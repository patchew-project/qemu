#!/usr/libexec/flua
--
-- SPDX-License-Identifier: BSD-2-Clause
--
-- Copyright (c) 2026 Warner Losh <imp@FreeBSD.org>
-- Copyright (c) 2024 Tyler Baxter <agge@FreeBSD.org>
-- Copyright (c) 2019 Kyle Evans <kevans@FreeBSD.org>
--

-- Add library root to the package path.
local path = arg[0]:gsub("/[^/]+.lua$", "")
package.path = package.path .. ";" .. path .. "/?.lua;" .. os.getenv('FREEBSD_SYSCALL_DIR') .. "/?.lua"

local FreeBSDSyscall = require("core.freebsd-syscall")
local generator = require("tools.generator")
local config = require("config")

-- File has not been decided yet; config will decide file.  Default defined as
-- /dev/null.
file = "/dev/stdout"

function generate(tbl, config, fh)
	-- Grab the master system calls table.
	local s = tbl.syscalls

	table.sort(s, function(a, b)
	    return a.arg_alias < b.arg_alias
	end)
	-- Bind the generator to the parameter file.
	local gen = generator:new({}, fh)
	gen.storage_levels = {}	-- make sure storage is clear

	-- Write the generated preamble.
	gen:preamble("FreeBSD strace list\nNOTE: Use syscall numbers so we work on all the branches.")

	for _, v in pairs(s) do
		local c = v:compatLevel()

		-- Handle non-compat:
		if v:native() then
			-- All these negation conditions are because (in
			-- general) these are cases where code for sysproto.h
			-- is not generated.
			if not v.type.NOARGS and not v.type.NOPROTO and
			    not v.type.NODEF then
				fmt = "NULL"
				fcn = "NULL"
				if v.arg_alias == "__sysctl" then
					fcn = "print_sysctl"
				elseif v.arg_alias == "execve" or v.arg_alias == "fexecve" then
					fcn = "print_execve"
				elseif v.arg_alias == "ioctl" then
					fcn = "print_ioctl"
				elseif v.arg_alias == "mmap" then
					fcn = "print_mmap"
				elseif v.arg_alias == "sysarch" then
					fcn = "print_sysarch"
				elseif #v.args > 0 then
					fmt = "\"%s("
					for _, arg in ipairs(v.args) do
						if arg.type == "char *" then
							fmt = fmt .. "%s, "
						elseif arg.type == "mode_t" then
							fmt = fmt .. "%o, "
						else
							fmt = fmt .. "%#x, "
						end
					end
					fmt = fmt:sub(1, -3)  .. ")\""
				end
				gen:write(string.format(
				    "{ %d, \"%s\", %s, %s, NULL },\n",
				    v.num, v.arg_alias, fmt, fcn))
			end
		-- Handle compat (everything >= FREEBSD9):
		elseif c >= 9 then
			local idx = c * 10
			if not v.type.NOARGS and not v.type.NOPROTO and
			    not v.type.NODEF then
				fmt = "NULL"
				if #v.args > 0 then
					fmt = "\"%s("
					for _, arg in ipairs(v.args) do
						if arg.type == "char *" then
							fmt = fmt .. "%s, "
						elseif arg.type == "mode_t" then
							fmt = fmt .. "%o, "
						else
							fmt = fmt .. "%#x, "
						end
					end
					fmt = fmt:sub(1, -3)  .. ")\""
				end
				gen:write(string.format(
				    "{ %d, \"%s\", %s, %s, NULL },\n",
				    v.num, v.arg_alias, fmt, fcn))
			end
		end
		-- Do nothing for obsolete, unimplemented, and reserved.
	end
end


if #arg < 1 or #arg > 2 then
	error("usage: " .. arg[0] .. " syscall.master")
end

local sysfile = arg[1]

config.merge(None)
config.mergeCompat()

-- The parsed system call table.
local tbl = FreeBSDSyscall:new{sysfile = sysfile, config = config}

file = arg[2] or "/dev/stdout"
generate(tbl, config, file)
