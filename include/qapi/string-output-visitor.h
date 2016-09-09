/*
 * String printing Visitor
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef STRING_OUTPUT_VISITOR_H
#define STRING_OUTPUT_VISITOR_H

#include "qapi/visitor.h"

typedef struct StringOutputVisitor StringOutputVisitor;

/*
 * Create a new string output visitor.
 *
 * Using @human creates output that is a bit easier for humans to read
 * (for example, showing integer values in both decimal and hex). The
 * output will only contain the values for the visited items, not the
 * field names. When encountering lists of integers, order of the list
 * elements will not be preserved in the output format. They will be
 * re-arranged in numerical order and contiguous values merged into
 * ranges. Strings will have double quotes added. If @human is set
 * to true, then integers will be printed in both decimal and hexidecimal
 * format. Some example outputs:
 *
 * - Single integer (human friendly)
 *    42 (0x2a)
 *
 * - List of integers
 *   0-1,3-6,9-16,21-22,9223372036854775806-9223372036854775807
 *
 * - Boolean
 *   true
 *
 * - Sring
 *   "QEMU"
 *
 * If everything succeeds, pass @result to visit_complete() to
 * collect the result of the visit.
 *
 * The string output visitor does not implement support for visiting
 * QAPI structs, alternates, null, or arbitrary QTypes.  It also
 * requires a non-null list argument to visit_start_list().
 *
 * For outputting of complex types, including the field names, the
 * TextOutputVisitor implementation is likely to be a better choice,
 * as it can deal with arbitrary nesting and will preserve ordering
 * of lists of integers.
 */
Visitor *string_output_visitor_new(bool human, char **result);

#endif
