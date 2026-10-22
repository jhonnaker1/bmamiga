/*
 * chd_nofiles.cpp - MAME's special file names for Amiberry on Circle, in
 * place of src/archivers/chd/posixsocket.cpp and posixptty.cpp.
 *
 * MAME's file layer opens "socket.host:port", "domain.path" and "pty"
 * as network connections and pseudo terminals. Circle has neither, and
 * Amiberry only opens disk images through it: such names are ordinary
 * files here.
 *
 * Compiled with Amiberry's flags (src/port/amiberry.mk, BMA_EXTRA).
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include "archivers/chd/osdfile.h"
#include "archivers/chd/posixfile.h"

bool posix_check_socket_path(std::string const &path) noexcept
{
	(void) path;
	return false;
}

std::error_condition posix_open_socket(std::string const &path, std::uint32_t openflags,
                                       osd_file::ptr &file, std::uint64_t &filesize) noexcept
{
	(void) path; (void) openflags; (void) file; (void) filesize;
	return std::errc::not_supported;
}

bool posix_check_domain_path(std::string const &path) noexcept
{
	(void) path;
	return false;
}

std::error_condition posix_open_domain(std::string const &path, std::uint32_t openflags,
                                       osd_file::ptr &file, std::uint64_t &filesize) noexcept
{
	(void) path; (void) openflags; (void) file; (void) filesize;
	return std::errc::not_supported;
}

bool posix_check_ptty_path(std::string const &path) noexcept
{
	(void) path;
	return false;
}

std::error_condition posix_open_ptty(std::uint32_t openflags, osd_file::ptr &file,
                                     std::uint64_t &filesize, std::string &name) noexcept
{
	(void) openflags; (void) file; (void) filesize; (void) name;
	return std::errc::not_supported;
}
