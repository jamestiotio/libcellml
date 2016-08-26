/*
Copyright libCellML Contributors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "utilities.h"

namespace libcellml {

bool canConvertToDouble(const std::string &candidate)
{
    bool canConvert = false;
    // Try to convert the input string to double.
    try
    {
        std::stod(candidate);
        canConvert = true;
    } catch (std::invalid_argument) {
        canConvert = false;
    } catch (std::out_of_range) {
        canConvert = false;
    }

    return canConvert;
}

bool EXPORT_FOR_TESTING hasNonWhitespaceCharacters(const std::string &input)
{
    return input.find_first_not_of(" \t\n\v\f\r") != input.npos;;
}

}
