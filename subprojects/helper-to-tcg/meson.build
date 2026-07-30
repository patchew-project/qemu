##
##  Copyright(c) 2026 rev.ng Labs Srl. All Rights Reserved.
##
##  This program is free software; you can redistribute it and/or modify
##  it under the terms of the GNU General Public License as published by
##  the Free Software Foundation; either version 2 of the License, or
##  (at your option) any later version.
##
##  This program is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##  GNU General Public License for more details.
##
##  You should have received a copy of the GNU General Public License
##  along with this program; if not, see <http://www.gnu.org/licenses/>.
##

project('helper-to-tcg', ['cpp'],
        version: '0.8',
        default_options: ['cpp_std=c++17'])

python = import('python').find_installation()

# Find LLVM using llvm-config manually.  Needed as meson struggles when multiple
# versions of LLVM are installed on the same system (always returns the most
# recent).
llvm_config = get_option('llvm_config_path')
cpp_args = run_command(llvm_config, '--cxxflags').stdout().strip().split()
bindir = run_command(llvm_config, '--bindir').stdout().strip()
ldflags = run_command(llvm_config, '--ldflags').stdout().strip().split()
libs = run_command(llvm_config, '--libs').stdout().strip().split()
syslibs = run_command(llvm_config, '--system-libs').stdout().strip().split()
incdir = run_command(llvm_config, '--includedir').stdout().strip().split()
version = run_command(llvm_config, '--version').stdout().strip()
version_major = version.split('.')[0].to_int()

# Check LLVM version manually
if version_major < 10 or version_major > 21
    error('LLVM version', version, 'not supported.')
endif

sources = [
    'src/LlvmCompat.cpp',
    'src/Pipeline.cpp',
    'src/PrepareForOptPass/PrepareForOptPass.cpp',
    'src/PseudoInst.cpp',
    'src/PrepareForTcgPass/PrepareForTcgPass.cpp',
    'src/PrepareForTcgPass/TransformGEPs.cpp',
    'src/PrepareForTcgPass/CanonicalizeIR.cpp',
    'src/PrepareForTcgPass/IdentityMap.cpp',
    'src/TcgGenPass/TcgGenPass.cpp',
    'src/TcgGenPass/LinearizeBlocks.cpp',
]

clang = bindir / 'clang'
llvm_link = bindir / 'llvm-link'

get_llvm_ir_cmd = [python, meson.current_source_dir() / 'get-llvm-ir.py',
                   '--compile-commands', 'compile_commands.json',
                   '--clang', clang,
                   '--llvm-link', llvm_link]

# NOTE: Add -Wno-template-id-cdtor for GCC versions >= 14.  This warning is
# related to a change in the C++ standard in C++20, that also applies to C++14
# for some reason. See defect report DR2237 and commit
#   https://gcc.gnu.org/git/gitweb.cgi?p=gcc.git;h=4b38d56dbac6742b038551a36ec80200313123a1
# (temporary)
compiler_info = meson.get_compiler('cpp')
compiler = compiler_info.get_id()
compiler_version = compiler_info.version().split('-').get(0)
compiler_version_major = compiler_version.split('.').get(0)
if compiler == 'gcc' and compiler_version_major.to_int() >= 14
    cpp_args += ['-Wno-template-id-cdtor', '-Wno-missing-template-keyword']
endif

pipeline = executable('helper-to-tcg', sources,
                      include_directories: ['include'] + [incdir],
                      link_args: [ldflags] + [libs] + [syslibs],
                      cpp_args: cpp_args)
