//
// Copyright (c) 2007 Ole Andr� Vadla Ravn�s <oleavr@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "Logger.h"

#include <ntstrsafe.h>

namespace oSpy {

HANDLE Logger::m_captureSection = NULL;
Capture * Logger::m_capture = NULL;
volatile ULONG Logger::m_index = 0;

void
Logger::Initialize ()
{
  m_captureSection = NULL;
  m_capture = NULL;

  UNICODE_STRING objName;
  RtlInitUnicodeString (&objName, L"\\BaseNamedObjects\\oSpyCapture");

  OBJECT_ATTRIBUTES attrs;
  InitializeObjectAttributes (&attrs, &objName, 0, NULL, NULL);

  NTSTATUS status;
  status = ZwOpenSection (&m_captureSection, SECTION_ALL_ACCESS, &attrs);
  if (NT_SUCCESS (status))
  {
    SIZE_T size = sizeof (Capture);

    status = ZwMapViewOfSection (m_captureSection, NtCurrentProcess (),
      reinterpret_cast <void **> (&m_capture), 0L, sizeof (Capture),
      NULL, &size, ViewUnmap, 0, PAGE_READWRITE | PAGE_NOCACHE);

    if (!NT_SUCCESS (status))
    {
      KdPrint (("ZwMapViewOfSection failed: 0x%08x", status));
    }
  }
  else
  {
    KdPrint (("ZwOpenSection failed: 0x%08x", status));
  }

  m_index = 0;
}

void
Logger::Shutdown ()
{
  NTSTATUS status;

  if (m_capture != NULL)
  {
    status = ZwUnmapViewOfSection (NtCurrentProcess (), m_capture);
    if (!NT_SUCCESS (status))
      KdPrint (("ZwUnmapViewOfSection failed: 0x%08x", status));
  }

  if (m_captureSection != NULL)
  {
    status = ZwClose (m_captureSection);
    if (!NT_SUCCESS (status))
      KdPrint (("ZwClose failed: 0x%08x", status));
  }
}

NTSTATUS
Logger::Start (const WCHAR * fnSuffix)
{
  NTSTATUS status = STATUS_SUCCESS;

  UNICODE_STRING logfilePath;
  WCHAR buffer[256];
  RtlInitEmptyUnicodeString (&logfilePath, buffer, sizeof (buffer));

  __try
  {
    if (m_capture != NULL)
      status = RtlUnicodeStringPrintf (&logfilePath,
        L"\\DosDevices\\%s\\oSpyDriverAgent-%s.log", m_capture->LogPath,
        fnSuffix);
    else
      status = RtlUnicodeStringPrintf (&logfilePath,
        L"\\SystemRoot\\oSpyDriverAgent-%s.log", fnSuffix);

    if (!NT_SUCCESS (status))
    {
      KdPrint (("RtlUnicodeStringPrintf failed: 0x%08x", status));
      return status;
    }

    KdPrint (("Logging to %S", buffer));

    m_fileHandle = NULL;

    KeInitializeEvent (&m_workEvent, NotificationEvent, FALSE);
    KeInitializeEvent (&m_stopEvent, NotificationEvent, FALSE);
    m_logThreadObject = NULL;

    ExInitializeSListHead (&m_items);
    KeInitializeSpinLock (&m_itemsLock);

    OBJECT_ATTRIBUTES attrs;
    InitializeObjectAttributes (&attrs, &logfilePath, 0, NULL, NULL);

    IO_STATUS_BLOCK ioStatus;
    status = ZwCreateFile (&m_fileHandle, GENERIC_WRITE, &attrs, &ioStatus,
      NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OVERWRITE_IF,
      FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS (status))
    {
      KdPrint (("ZwCreateFile failed: 0x%08x", status));
      return status;
    }

    HANDLE threadHandle;
    status = PsCreateSystemThread (&threadHandle, 0, NULL,
      0, NULL, LogThreadFuncWrapper, this);
    if (!NT_SUCCESS (status))
    {
      KdPrint (("PsCreateSystemThread failed: 0x%08x", status));
      return status;
    }

    status = ObReferenceObjectByHandle (threadHandle, THREAD_ALL_ACCESS, NULL,
      KernelMode, &m_logThreadObject, NULL);

    ZwClose (threadHandle);

    if (!NT_SUCCESS (status))
    {
      KdPrint (("ObReferenceObjectByHandle failed: 0x%08x", status));
      return status;
    }
  }
  __finally
  {
    if (!NT_SUCCESS (status))
    {
      if (m_fileHandle != NULL)
      {
        ZwClose (m_fileHandle);
        m_fileHandle = NULL;
      }
    }
  }

  return status;
}

void
Logger::Stop ()
{
  KeSetEvent (&m_stopEvent, IO_NO_INCREMENT, FALSE);
  KeWaitForSingleObject (m_logThreadObject, Executive, KernelMode, FALSE,
    NULL);
  ObDereferenceObject (m_logThreadObject);
}

void
Logger::LogThreadFunc ()
{
  NTSTATUS status;
  
  KdPrint (("Logger thread speaking"));

  bool done = false;

  void * waitObjects[2] = { &m_workEvent, &m_stopEvent };

  do
  {
    status = KeWaitForMultipleObjects (2, waitObjects, WaitAny, Executive,
      KernelMode, FALSE, NULL, NULL);
    if (status == STATUS_WAIT_0)
      KeClearEvent (&m_workEvent);

    ProcessItems ();
  }
  while (status != STATUS_WAIT_1);

  if (m_fileHandle != NULL)
  {
    ZwClose (m_fileHandle);
    m_fileHandle = NULL;
  }

  KdPrint (("Logger thread leaving"));

  PsTerminateSystemThread (STATUS_SUCCESS);
}

void
Logger::ProcessItems ()
{
  SLIST_ENTRY * listEntry;

  while ((listEntry =
    ExInterlockedPopEntrySList (&m_items, &m_itemsLock)) != NULL)
  {
    LogEntry * entry = reinterpret_cast <LogEntry *> (listEntry);

    if (m_capture != NULL)
    {
      InterlockedIncrement (reinterpret_cast<volatile LONG *>
        (&m_capture->LogCount));
      InterlockedExchangeAdd (reinterpret_cast<volatile LONG *>
        (&m_capture->LogSize), sizeof (LogEntry));
    }

    WriteNode (&entry->event);

    DestroyEntry (entry);
  }
}

void
Logger::DestroyEntry (LogEntry * entry)
{
  entry->event.Destroy ();

  ExFreePoolWithTag (entry, 'SpSo');
}

Event *
Logger::NewEvent (const char * eventType,
                  int childCapacity,
                  void * userData)
{
  LogEntry * logEntry = static_cast <LogEntry *> (
    ExAllocatePoolWithTag (NonPagedPool, sizeof (LogEntry), 'SpSo'));
  if (logEntry == NULL)
  {
    KdPrint (("ExAllocatePoolWithTag failed\n"));
    return NULL;
  }

  ULONG id = InterlockedIncrement (reinterpret_cast<volatile LONG *> (&m_index));
  LARGE_INTEGER timestamp;
  KeQuerySystemTime (&timestamp);

  Event * ev = &logEntry->event;
  ev->Initialize (id, timestamp, eventType, childCapacity);
  ev->m_userData = userData;

  //KdPrint (("Logger::NewEvent: ev=%p, id=%d\n", ev, id));
  return ev;
}

void
Logger::SubmitEvent (Event * ev)
{
  //KdPrint (("Logger::SubmitEvent: ev=%p, id=%s\n", ev, ev->m_fieldValues[0]));

  LogEntry * logEntry = reinterpret_cast <LogEntry *> (
    reinterpret_cast <UCHAR *> (ev) - sizeof (SLIST_ENTRY));

  ExInterlockedPushEntrySList (&m_items, &logEntry->entry, &m_itemsLock);
  KeSetEvent (&m_workEvent, IO_NO_INCREMENT, FALSE);
}

void
Logger::CancelEvent (Event * ev)
{
  //KdPrint (("Logger::CancelEvent: ev=%p, id=%s\n", ev, ev->m_fieldValues[0]));

  LogEntry * logEntry = reinterpret_cast <LogEntry *> (
    reinterpret_cast <UCHAR *> (ev) - sizeof (SLIST_ENTRY));

  DestroyEntry (logEntry);
}

void
Logger::WriteNode (const Node * node)
{
  Write (node->m_name);

  int fieldCount = node->GetFieldCount ();
  Write (fieldCount);
  for (int i = 0; i < fieldCount; i++)
  {
    Write (node->m_fieldKeys[i]);
    Write (node->m_fieldValues[i]);
  }

  Write (node->m_contentIsRaw);
  Write (node->m_contentSize);
  WriteRaw (node->m_content, node->m_contentSize);

  int childCount = node->GetChildCount ();
  Write (childCount);
  for (int i = 0; i < childCount; i++)
  {
    WriteNode (node->m_children[i]);
  }
}

void
Logger::WriteRaw (const void * data, size_t dataSize)
{
  if (m_fileHandle == NULL || data == NULL || dataSize == 0)
    return;

  NTSTATUS status;
  IO_STATUS_BLOCK io_status;
  status = ZwWriteFile (m_fileHandle, NULL, NULL, NULL, &io_status,
    const_cast <void *> (data), static_cast <ULONG> (dataSize), 0, NULL);
  if (!NT_SUCCESS (status))
    KdPrint (("ZwWriteFile failed: 0x%08x", status));
}

void
Logger::Write (ULONG dw)
{
  WriteRaw (&dw, sizeof (dw));
}

void
Logger::Write (const char * str)
{
  if (str == NULL)
  {
    Write (static_cast <ULONG> (0));
    return;
  }

  size_t length = strlen (str);
  Write (static_cast <ULONG> (length));
  WriteRaw (str, length);
}

} // namespace oSpy
