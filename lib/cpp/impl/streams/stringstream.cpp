/**
 * $Id$
 * Copyright (C) 2008 - 2009 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <impl/streams/stringstream.h>

namespace std {
	stringstream::stringstream(ios_base::openmode which)
		: iostream(new stringbuf(which)) {
	}
	stringstream::stringstream(const string& s,
			ios_base::openmode which)
		: iostream(new stringbuf(s,which)) {
	}
	stringstream::~stringstream() {
	}

	stringbuf* stringstream::rdbuf() const {
		return static_cast<stringbuf*>(ios::rdbuf());
	}
	string stringstream::str() const {
		return rdbuf()->str();
	}
	void stringstream::str(const string& s) {
		rdbuf()->str(s);
	}
}
