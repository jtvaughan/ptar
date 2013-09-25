Title:		Plain Text Archive (ptar) Format  
Author:		Jordan Vaughan  
Date:		2013-09-24  
Encoding:	utf-8  
Format:		Markdown with MultiMarkdown metadata extensions  
Copyright:	Written in 2013 by Jordan Vaughan.  To the extent possible under law, Jordan Vaughan has waived all copyright and related or neighboring rights to this publication.  You can copy, modify, distribute and perform this publication, even for commercial purposes, all without asking permission.  Please see <http://creativecommons.org/publicdomain/zero/1.0/> for more information.

# Introduction
This is a detailed explanation of the plain text archive (ptar) format.

# Signatures
Archives begin with a signature

> ###BEGIN PTARv*X*###

where *X* stands for ptar format revision. (This document describes revision 0.)

End of archive is denoted with a signature

> ###END PTAR###

Only the data between these signatures is processed.

# Metadata Format
Metadata is plain text.  Archive bodies begin with a block of archive metadata and are followed by zero or more file entries.

Each piece of metadata occupies its own line and has two components: a key and a value.	Keys are case-insensitive and may contain letters, numbers, spaces, hyphens, and underscores, but each must begin with a letter or number, which must be the line’s first character.  Values may contain any character except NUL characters and newlines.  Programs that process plain text archives must strip keys of spaces and transform all key characters into their lowercase equivalents.  Thus “`File Size`”, “`F I L E si ze`”, and “`filesize`” are equivalent.  A key is separated from its value by a single colon character (`:`).

Keys other than those specified below are illegal.

# Standard Arhive Metadata Keys

* `Metadata Encoding`: This specifies the archive’s metadata’s character encoding, which must be one of `utf-8` (Unicode octet encoding), `utf8` (a synonym for `utf-8`), or `ascii` (7-bit US-ASCII).  If this key is missing, then `utf-8` is assumed.
* `Archive Creation Date`: This specifies the date the archive was created.  The format is arbitrary: It’s meant to be merely advisory.  However, a nice, standard format is `%Y-%m-%dT%H:%M:%SZ`, where `%Y` is the year, `%m` is the month (`00` to `12`), `%d` is the day of the month (`00` to `31`), `%H` is the hour (`00` to `23`), `%M` is the minute (`00` to `59`), and `%S` is the second (`00` to `60`, where `60` is for leap seconds).  In this case, the time is interpreted as Coordinated Universal Time (UTC; Zulu).  This key is not required.
* `Extensions`: This specifies a comma-separated list of names of format extensions used by the archive.  Extension names may contain any character except NUL characters, newlines, and commas.  Programs that process plain text archives must strip extension names of spaces and transforms their characters into their lowercase equivalents.  If this key is missing, then no extensions are present.

# Standard File Entry Metadata Keys

Technical terms appearing here, such as “symbolic link” and “major number,” are standard UNIX/*NIX terms.

* `Path`: the path of the file represented by the file entry, which may be relative or absolute
* `Type`: one of the following self-explanatory, case-insensitive identifiers for the file’s type: `Regular File`, `Directory`, `Symbolic Link`, `Character Device`, `Block Device`, `FIFO`, or `Socket`.
* `File Size`: the size of the file in bytes (decimal; only required for regular files)
* `Link Target`: path to the symbolic link’s target (only required for symbolic links)
* `Major`: the device’s major number (decimal; only required for character and block devices)
* `Minor`: the device’s minor number (decimal; only required for character and block devices)
* `User Name`: the file’s owner’s username
* `User ID`: the file’s owner’s user ID (decimal)
* `Group Name`: the file’s owner’s group name
* `Group ID`: the file’s owner’s group ID (decimal)
* `Permissions`: the file’s access permissions (at least four octal digits)
* `Modification Time`: the time the file’s contents were last changed in seconds since the Epoch (January 1, 1970, 00:00 Coordinated Universal Time) (decimal)

NOTE: Unless otherwise specified, all of the aforementioned keys are required.  Keys that do not apply to a file entry are silently ignored.

# Regular File Contents

If a file is a regular file (`Type` is `Regular File`), then the entry’s metadata is immediately followed by a line containing exactly three hyphens (`-`) followed by a newline character, then the file’s contents, then three hyphens followed by a newline character.  File entries must be separated by at least one blank line.  (Blank lines are optional after regular file entries because the final hyphens are implicit entry separators.)

# Example Archive

	Metadata Encoding: utf-8
	Archive Creation Date: 2013-09-24T22:41:20Z
	
	Path: a.txt
	Type: Regular File
	File Size: 29
	User Name: foo
	User ID: 1000
	Group Name: bar
	Group ID: 1001
	Permissions: 0000664
	Modification Time: 1380015036
	---
	Tue Sep 24 18:30:36 JST 2013
	---
	
	Path: b.txt
	Type: Regular File
	File Size: 29
	User Name: foo
	User ID: 1000
	Group Name: baz
	Group ID: 522
	Permissions: 0000664
	Modification Time: 1380015048
	---
	Tue Sep 24 18:30:48 JST 2013
	---
