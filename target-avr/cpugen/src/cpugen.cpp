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

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <limits>
#include <stdint.h>
#include <algorithm>
#include <iomanip>
#include <string>
#include <vector>
#include <boost/regex.hpp>

#include "yaml-cpp/yaml.h"
#include "tinyxml2/tinyxml2.h"

#include "utils.h"

#include <boost/algorithm/string.hpp>

struct  inst_info_t {
    std::string name;
    std::string opcode;

    tinyxml2::XMLElement *nodeFields;
};

struct  cpu_info_t {
    std::string name;
    std::vector<inst_info_t * > instructions;
};

int countbits(uint64_t value)
{
    int counter = 0;
    uint64_t mask = 1;

    for (size_t i = 0; i < sizeof(value) * 8; ++i) {
        if (value & mask) {
            counter++;
        }

        mask <<= 1;
    }

    return counter;
}

int encode(uint64_t mask, uint64_t value)
{
    uint64_t i = 0x0000000000000001;
    uint64_t j = 0x0000000000000001;
    uint64_t v = 0x0000000000000000;

    for (size_t it = 0; it < sizeof(value) * 8; ++it) {
        if (mask & i) {
            if (value & j) {
                v |= i;
            }
            j <<= 1;
        }

        i <<= 1;
    }

    return v;
}

std::string num2hex(uint64_t value)
{
    std::ostringstream  str;
    str << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;

    return str.str();
}

tinyxml2::XMLDocument   doc;

void operator >> (const YAML::Node & node, inst_info_t & info)
{
    for (auto it = node.begin(); it != node.end(); ++it) {
        const YAML::Node & curr = it->second;
        std::string name = it->first.as<std::string>();

        info.opcode = curr["opcode"].as<std::string>();

        const char *response;
        std::vector<std::string> fields;
        std::string opcode = "";
        int offset;
        tinyxml2::XMLElement *nodeFields = doc.NewElement("fields");
        uint32_t bitoffset = 0;

        do {
            opcode = info.opcode;
            boost::replace_all(info.opcode, "  ", " ");
            boost::replace_all(info.opcode, "0 0", "00");
            boost::replace_all(info.opcode, "0 1", "01");
            boost::replace_all(info.opcode, "1 0", "10");
            boost::replace_all(info.opcode, "1 1", "11");
        } while (opcode != info.opcode);

        boost::replace_all(info.opcode, "- -", "--");

        fields = boost::split(fields, info.opcode, boost::is_any_of(" "));

        opcode = "";
        info.opcode = "";
        unsigned f = 0;
        for (int i = 0; i < fields.size(); i++) {
            std::string field = fields[i];

            if (field.empty()) {
                continue;
            }

            size_t len = field.length();
            boost::cmatch match;
            tinyxml2::XMLElement *nodeField = doc.NewElement("field");

            nodeFields->LinkEndChild(nodeField);

            if (boost::regex_match(field.c_str(),
                        match,
                        boost::regex("^[01]+$"))) {
                int length = field.length();

                nodeField->SetAttribute("name", field.c_str());
                nodeField->SetAttribute("length", length);
                nodeField->SetAttribute("offset", bitoffset);

                info.opcode += field;

                bitoffset   += len;
            } else if (boost::regex_match(
                    field.c_str(),
                    match,
                    boost::regex("^[-]+$")))
            {
                int         length = field.length();

                nodeField->SetAttribute("name", "RESERVED");
                nodeField->SetAttribute("length", length);
                nodeField->SetAttribute("offset", bitoffset);

                info.opcode += field;

                bitoffset   += len;
            } else if (boost::regex_match(field.c_str(),
                    match,
                    boost::regex("^([a-zA-Z][a-zA-Z0-9]*)\\[([0-9]+)\\]"))) {
                int         length = std::atoi(match[2].first);
                std::string name = std::string(match[1].first, match[1].second);

                nodeField->SetAttribute("name", name.c_str());
                nodeField->SetAttribute("length", length);
                nodeField->SetAttribute("offset", bitoffset);

                for (int j = 0; j < length; j++) {
                    info.opcode += 'a' + f;
                }

                f++;

                bitoffset   += length;
            } else if (field == "~") {
                /* nothing */
            } else {
                std::cout << "cannot parse " << name
                          << ": '" << field << "'" << std::endl;
                exit(0);
            }
        }

        info.nodeFields = nodeFields;
        info.name = name;
    }
}

void operator >> (inst_info_t & info, tinyxml2::XMLElement & node)
{
    node.SetAttribute("length", (unsigned)info.opcode.length());
    node.SetAttribute("name", info.name.c_str());
    node.SetAttribute("opcode", info.opcode.c_str());
}

void operator >> (const YAML::Node & node, cpu_info_t & cpu)
{
    const YAML::Node & insts = node["instructions"];

    cpu.name = node["name"].as<std::string>();

    for (unsigned i = 0; i < insts.size(); i++) {
        inst_info_t *inst = new inst_info_t();

        insts[i] >> (*inst);

        if (inst->opcode != "" &&inst->opcode != "~") {
            cpu.instructions.push_back(inst);
        }
    }
}

std::pair<size_t, size_t> getMinMaxInstructionLength(
                                std::vector<inst_info_t * > &instructions)
{
    size_t  min = std::numeric_limits<size_t>::max();
    size_t  max = std::numeric_limits<size_t>::min();

    for (size_t i = 0; i < instructions.size(); i++) {
        inst_info_t *inst = instructions[i];
        std::string opcode = inst->opcode;
        size_t length = opcode.length();

        if (opcode != "~") {
            min = std::min(min, length);
            max = std::max(max, length);
        }
    }

    return std::make_pair(min, max);
}

uint64_t getXs(std::string const &opcode, size_t len, char chr)
{
    uint64_t result = 0;
    size_t cur;
    uint64_t bit = 1ull << (len - 1);

    for (cur = 0; cur < len; cur++) {
        if (opcode[cur] == chr) {
            result  |= bit;
        }

        bit >>= 1;
    }

    return result;
}

uint64_t get0s(std::string const &opcode, size_t len)
{
    return getXs(opcode, len, '0');
}

uint64_t get1s(std::string const &opcode, size_t len)
{
    return getXs(opcode, len, '1');
}

class InstSorter
{
    public:
        InstSorter(size_t offset, size_t length)
            : offset(offset), length(length)
        {

        }

        bool operator()(inst_info_t *a, inst_info_t *b)
        {
            uint64_t field0;
            uint64_t field1;
            uint64_t fieldA;
            uint64_t fieldB;

            field0 = get0s(a->opcode, length);
            field1 = get1s(a->opcode, length);
            fieldA = field0 | field1;

            field0 = get0s(b->opcode, length);
            field1 = get1s(b->opcode, length);
            fieldB = field0 | field1;

            return fieldB < fieldA;
        }

    private:
        size_t offset;
        size_t length;

};

void divide(uint64_t select0, uint64_t select1,
            std::vector<inst_info_t * > &info,
            size_t level, tinyxml2::XMLElement *root)
{
    std::pair<size_t, size_t>   minmaxSize;

    minmaxSize = getMinMaxInstructionLength(info);

    size_t minlen = minmaxSize.first;
    size_t maxlen = minmaxSize.second;
    size_t bits = std::min(minlen, sizeof(select0) * 8);
    uint64_t all1 = (1ULL << bits) - 1;
    uint64_t all0 = (1ULL << bits) - 1;
    uint64_t allx = (1ULL << bits) - 1;
    uint64_t diff;

    for (size_t i = 0; i < info.size(); ++i) {
        std::string opcode = info[i]->opcode;
        uint64_t field0 = get0s(opcode, minlen);
        uint64_t field1 = get1s(opcode, minlen);
        uint64_t fieldx = field0 | field1;

        if (opcode == "~") {
            continue;
        }
        all0 &= field0;
        all1 &= field1;
        allx &= fieldx;
    }

    diff = allx ^ (all0 | all1);

    if (diff == 0) {
        tinyxml2::XMLElement *oopsNode = doc.NewElement("oops");
        oopsNode->SetAttribute("bits", (unsigned)bits);
        oopsNode->SetAttribute("maxlen", (unsigned)maxlen);
        oopsNode->SetAttribute("allx", num2hex(allx).c_str());
        oopsNode->SetAttribute("all0", num2hex(all0).c_str());
        oopsNode->SetAttribute("all1", num2hex(all1).c_str());
        oopsNode->SetAttribute("select0", num2hex(select0).c_str());
        oopsNode->SetAttribute("select1", num2hex(select1).c_str());
        root->LinkEndChild(oopsNode);

        std::sort(info.begin(), info.end(), InstSorter(0, minlen));

        for (size_t i = 0; i < info.size(); ++i) {
            inst_info_t *inst = info[i];
            tinyxml2::XMLElement *instNode = doc.NewElement("instruction");
            tinyxml2::XMLElement *matchNode = doc.NewElement("match01");

            uint64_t field0 = get0s(inst->opcode, minlen);
            uint64_t field1 = get1s(inst->opcode, minlen);
            uint64_t fieldx = field0 | field1;

            root->LinkEndChild(matchNode);
            matchNode->LinkEndChild(instNode);

            matchNode->SetAttribute("mask", num2hex(fieldx).c_str());
            matchNode->SetAttribute("value", num2hex(field1).c_str());

            *inst >> *instNode;

            instNode->LinkEndChild(inst->nodeFields);
        }

        return;
    }

    uint64_t    bitsN = countbits(diff); /* number of meaningfull bits */

    tinyxml2::XMLElement *switchNode = doc.NewElement("switch");
    switchNode->SetAttribute("bits", (unsigned)bits);
    switchNode->SetAttribute("bitoffset", (unsigned)0);
    switchNode->SetAttribute("mask", num2hex(diff).c_str());
    root->LinkEndChild(switchNode);

    /* there are at most 1 << length subsets */
    for (size_t s = 0; s < (1 << bitsN); ++s) {
        std::vector<inst_info_t * > subset;
        uint64_t index = encode(diff, s);

        tinyxml2::XMLElement *caseNode = doc.NewElement("case");
        caseNode->SetAttribute("value", num2hex(index).c_str());
        switchNode->LinkEndChild(caseNode);

        for (size_t i = 0; i < info.size(); ++i) {
            std::string opcode = info[i]->opcode;
            uint64_t field0 = get0s(opcode, minlen);
            uint64_t field1 = get1s(opcode, minlen);

            if (((field0 & diff) == (~index & diff))
                && ((field1 & diff) == (index  & diff))) {
                subset.push_back(info[i]);
            }
        }

        if (subset.size() == 1) {
            inst_info_t *inst = subset[0];
            tinyxml2::XMLElement *instNode = doc.NewElement("instruction");

            *inst >> *instNode;

            instNode->LinkEndChild(inst->nodeFields);

            caseNode->LinkEndChild(instNode);
        } else if (subset.size() > 1) {
            /* this is a set of instructions, continue dividing */
            divide(select0 | (diff & ~index),
                   select1 | (diff & index),
                   subset,
                   level + 2,
                   caseNode);
        }
    }
}

void generateParser(cpu_info_t & cpu)
{
    tinyxml2::XMLElement *cpuNode = doc.NewElement("cpu");
    tinyxml2::XMLElement *instNode = doc.NewElement("instructions");

    cpuNode->SetAttribute("name", cpu.name.c_str());
    cpuNode->LinkEndChild(instNode);

    doc.LinkEndChild(cpuNode);

    divide(0, 0, cpu.instructions, 1, instNode);

    doc.SaveFile("output.xml");
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::cerr << "error: usage: cpuarg [input.yaml]" << std::endl;
        std::exit(0);
    }

    try {
        const char *filename = argv[1];
        std::ifstream input(filename);
        YAML::Node doc = YAML::Load(input);
        cpu_info_t cpu;

        doc["cpu"] >> cpu;

        generateParser(cpu);
    } catch(const YAML::Exception & e) {
        std::cerr << e.what() << "\n";
    }
}
