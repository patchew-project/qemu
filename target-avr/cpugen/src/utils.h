/*
 * CPUGEN
 *
 * Copyright (c) 2016 Michael Rolnik
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef UTILS_H_
#define UTILS_H_

#include <string>
#include <vector>
#include <iostream>
#include <iomanip>

typedef std::vector<std::string>    string_vector_t;

std::string extract(std::string & str, std::string delimiter);
std::string rextract(std::string & str, std::string del);
string_vector_t split(std::string str, std::string delimeter);
std::string join(string_vector_t const &vec, std::string delimeter);

int countbits(uint64_t value);
int encode(uint64_t mask, uint64_t value);
std::string num2hex(uint64_t value);

class multi
{
/*
    http://www.angelikalanger.com/Articles/Cuj/05.Manipulators/Manipulators.html
*/
    public:
        multi(char c, size_t n)
            : how_many_(n)
            , what_(c)
        {
        }

    private:
        const size_t    how_many_;
        const char      what_;

    public:
        template <class Ostream>
        Ostream & apply(Ostream & os) const
        {
            for (unsigned int i = 0; i < how_many_; ++i) {
                os.put(what_);
            }
            os.flush();
            return os;
        }
};

template <class Ostream>
Ostream & operator << (Ostream & os, const multi & m)
{
    return m.apply(os);
}

#endif
