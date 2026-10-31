# SPDX-License-Identifier: GPL-3.0-or-later
#
# helix_read_app_srcs(<manifest> <out-var>): the repo-relative paths app_srcs.txt lists.
#
# A line holding one .cpp/.c path is compiled, with any surrounding whitespace and an
# optional trailing '# comment'; a line starting with '#' is a comment. The REGEX keeps
# comment lines out of the list entirely, since their ';' and '[' are CMake list
# syntax. scripts/check_esp32_app_srcs.py parses the file the same way and refuses a
# path line carrying ';', '[' or ']'.
function(helix_read_app_srcs manifest out_var)
    file(STRINGS "${manifest}" lines
         REGEX "^[ \t]*[^# \t][^#]*\\.(cpp|c)[ \t]*(#.*)?$")
    set(paths "")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "#.*$" "" path "${line}")
        string(STRIP "${path}" path)
        list(APPEND paths "${path}")
    endforeach()
    set(${out_var} "${paths}" PARENT_SCOPE)
endfunction()
