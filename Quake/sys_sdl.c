/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include "sys.h"

#include <errno.h>

typedef struct file_handle_s
{
	FILE	   *file;
	const byte *memory;
	int			owns_memory; /* memory[] was malloc'd by a slurping Sys_FileOpenRead -> free() on close */
	int			pos;
	int			size;
} file_handle_t;

#define MAX_HANDLES 32 /* johnfitz -- was 10 */
static file_handle_t sys_handles[MAX_HANDLES];

static int findhandle (void)
{
	int i;

	for (i = 1; i < MAX_HANDLES; i++)
	{
		if (!sys_handles[i].file && !sys_handles[i].memory)
			return i;
	}
	Sys_Error ("out of handles");
	return -1;
}

qfileofs_t Sys_filelength (FILE *f)
{
	qfileofs_t pos, end;

	pos = Sys_ftell (f);
	Sys_fseek (f, 0, SEEK_END);
	end = Sys_ftell (f);
	Sys_fseek (f, pos, SEEK_SET);

	return end;
}

qfileofs_t Sys_FileOpenRead (const char *path, int *hndl)
{
	FILE *f;
	int	  i;
	long  len;
	byte *buf;

	i = findhandle ();
	f = fopen (path, "rb");
	if (!f)
	{
		*hndl = -1;
		return -1;
	}

	/* Slurp the whole file into RAM and CLOSE it immediately, then serve reads/seeks
	 * from the buffer (via the memory[] handle path below). Two reasons:
	 *   1. pak0.pak over NFS is otherwise read per-lump (hundreds of slow random reads).
	 *   2. CRITICAL for demo playback: upstream kept the pak FILE* open for the whole
	 *      session, so CL_PlayDemo's second fopen() on the SAME pak was a concurrent
	 *      stream — which libphoenix cannot read (fread returns 0), so demos silently
	 *      CL_StopPlayback at signon 0. Closing here leaves the demo's fopen the sole
	 *      OS stream on the file. (Mirrors the working quakespasm-port Sys_FileOpenRead.) */
	fseek (f, 0, SEEK_END);
	len = ftell (f);
	fseek (f, 0, SEEK_SET);
	if (len < 0)
		len = 0;
	buf = (byte *) malloc ((size_t) (len > 0 ? len : 1)); /* >=1 byte so memory[]!=NULL marks the handle used */
	if (!buf)
	{
		fclose (f);
		*hndl = -1;
		return -1;
	}
	if (len > 0 && fread (buf, 1, (size_t) len, f) != (size_t) len)
	{
		free (buf);
		fclose (f);
		*hndl = -1;
		return -1;
	}
	fclose (f);

	sys_handles[i].file = NULL;
	sys_handles[i].memory = buf;
	sys_handles[i].owns_memory = 1;
	sys_handles[i].size = (int) len;
	sys_handles[i].pos = 0;
	*hndl = i;
	return (qfileofs_t) len;
}

void Sys_MemFileOpenRead (const byte *memory, int size, int *hndl)
{
	int i = findhandle ();

	sys_handles[i].memory = memory;
	sys_handles[i].size = size;
	sys_handles[i].pos = 0;
	*hndl = i;
}

int Sys_FileOpenWrite (const char *path)
{
	FILE *f;
	int	  i;

	i = findhandle ();
	f = fopen (path, "wb");

	if (!f)
		Sys_Error ("Error opening %s: %s", path, strerror (errno));

	sys_handles[i].file = f;
	return i;
}

void Sys_FileClose (int handle)
{
	if (sys_handles[handle].file)
	{
		fclose (sys_handles[handle].file);
		sys_handles[handle].file = NULL;
	}
	else if (sys_handles[handle].owns_memory && sys_handles[handle].memory)
	{
		free ((void *) sys_handles[handle].memory); /* slurped read buffer (NOT the static embedded pak) */
	}
	sys_handles[handle].memory = NULL;
	sys_handles[handle].owns_memory = 0;
	sys_handles[handle].size = 0;
	sys_handles[handle].pos = 0;
}

void Sys_FileSeek (int handle, int position)
{
	if (sys_handles[handle].file)
		fseek (sys_handles[handle].file, position, SEEK_SET);
	else
		sys_handles[handle].pos = position;
}

int Sys_FileRead (int handle, void *dest, int count)
{
	if (sys_handles[handle].file)
		return fread (dest, 1, count, sys_handles[handle].file);
	else
	{
		int avail = sys_handles[handle].size - sys_handles[handle].pos;
		if (count > avail)
			count = avail;
		if (count <= 0)
			return 0;
		memcpy (dest, sys_handles[handle].memory + sys_handles[handle].pos, count);
		sys_handles[handle].pos += count;
		return count;
	}
}

int Sys_FileWrite (int handle, const void *data, int count)
{
	assert (sys_handles[handle].file);
	return fwrite (data, 1, count, sys_handles[handle].file);
}
