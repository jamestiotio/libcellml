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

#pragma once

#include "libcellml/types.h"

#include "namedentity_p.h"

namespace libcellml {

using VariableWeakPtr = std::weak_ptr<Variable>; /**< Type definition for weak variable pointer. */

/**
 * @brief The Variable::VariableImpl struct.
 *
 * The private implementation for the Variable class.
 */
class VariablePrivate : public NamedEntityPrivate
{
public:
    Variable *mVariable = nullptr;
    std::vector<VariableWeakPtr> mEquivalentVariables; /**< Equivalent variables for this Variable.*/
    std::map<VariableWeakPtr, std::string, std::owner_less<VariableWeakPtr>> mMappingIdMap; /**< Mapping id map for equivalent variable.*/
    std::map<VariableWeakPtr, std::string, std::owner_less<VariableWeakPtr>> mConnectionIdMap; /**< Connection id map for equivalent variable.*/
    std::string mInitialValue; /**< Initial value for this Variable.*/
    std::string mInterfaceType; /**< Interface type for this Variable.*/
    UnitsPtr mUnits = nullptr; /**< The Units defined for this Variable.*/

    /**
     * @brief Clean expired equivalent variables.
     *
     * Clean away any equivalent variables that have become expired.
     */
    void cleanExpiredVariables();

    /**
     * @brief Private function to add an equivalent variable to the set for this variable.
     *
     * Add the argument equivalent variable to the set of equivalent variables for this
     * variable if it is not already present. If the equivalent variable is present,
     * do nothing.
     *
     * @sa addEquivalence, unsetEquivalentTo
     *
     * @param equivalentVariable The variable to add to this variable's equivalent
     * variable set if not already present.
     *
     * @return True if the given variable is added to this variables equivalent variable set.
     */
    bool setEquivalentTo(const VariablePtr &equivalentVariable);

    /**
     * @brief Private function to remove an equivalent variable from the set for this variable.
     *
     * Remove the @p equivalentVariable from the set of equivalent variables for this
     * variable if it is present.
     *
     * @sa removeEquivalence, setEquivalentTo
     *
     * @param equivalentVariable The variable to remove from this variable's equivalent
     * variable set if it is present.
     *
     * @return True if the @p equivalentVariable is removed from set of equivalent variables, false otherwise.
     */
    bool unsetEquivalentTo(const VariablePtr &equivalentVariable);

    /**
     * @brief Test if the given variable is directly equivalent to this one.
     *
     * The two variables are considered directly equivalent if this variable holds a valid reference to the
     * given variable.  Returns @c true if this variable holds a reference to the given variable
     * and that that reference is a valid reference to the given variable.
     *
     * @param equivalentVariable The variable to test for equivalence to this one.
     * @return @c true if the variables are equivalent @c false otherwise.
     */
    bool hasEquivalentVariable(const VariablePtr &equivalentVariable, bool considerIndirectEquivalences = false) const;

    /**
     * @brief Test whether the argument variable is equivalent to this variable.
     *
     * Tests to see if the argument variable is equivalent to this variable. Returns @c true if
     * the argument variable is equivalent to this variable and @c false otherwise. The test will
     * traverse through the equivalent network to determine if the argument variable is
     * equivalent to this variable.
     *
     * @param equivalentVariable The variable to check for equivalence.
     *
     * @return @c true if the @p equivalentVariable is equivalent to this variable and
     * @c false otherwise.
     */
    bool hasIndirectEquivalentVariable(const VariablePtr &equivalentVariable) const;

    /**
     * @brief Set the equivalent mapping id for this equivalence.
     *
     * Record the given id as the mapping id for the equivalence defined with this variable
     * and the given one.  This id appears in the 'map_variables' element of the model when
     * serialised.
     *
     * To clear an equivalence mapping id set it to the empty string. If the two variables are
     * not equivalent the mapping id is not set.
     *
     * @param equivalentVariable The equivalent variable the id refers to.
     * @param id @c std::string id to set.
     */
    void setEquivalentMappingId(const VariablePtr &equivalentVariable, const std::string &id);

    /**
     * @brief Set the equivalent connection id for this equivalence.
     *
     * Record the given id as the connection id for the equivalence defined with this variable
     * and the given one.  This id appears in the 'connection' element of the model when serialised.
     *
     * Where the same component pair has multiple equivalent variables only the last connection id
     * found in the set will be serialised.
     *
     * To clear an equivalence connection id set it to the empty string.  If the two variables are not
     * equivalent the connection id is not set.
     *
     * @param equivalentVariable The equivalent variable the id refers to.
     * @param id @c std::string id to set.
     */
    void setEquivalentConnectionId(const VariablePtr &equivalentVariable, const std::string &id);

    /**
     * @brief Get the equivalent mapping id for this equivalence.
     *
     * Get the mapping id set for the equivalence defined by the this variable and the given one.
     * If no mapping id is set the empty string is returned.
     *
     * If the two variables are not equivalent the empty string is returned.
     *
     * @param equivalentVariable The variable this variable is equivalent to.
     * @return The @c std::string id of the equivalence if found otherwise returns the empty string.
     */
    std::string equivalentMappingId(const VariablePtr &equivalentVariable) const;

    /**
     * @brief Get the equivalent connection id for this equivalence.
     *
     * Get the connection id set for the equivalence defined by the this variable and the given one.
     * If no connection id is set the empty string is returned.
     *
     * If the two variables are not equivalent the empty string is returned.
     *
     * @param equivalentVariable The variable this variable is equivalent to.
     * @return The @c std::string id of the equivalence if found otherwise returns the empty string.
     */
    std::string equivalentConnectionId(const VariablePtr &equivalentVariable) const;

    std::vector<VariableWeakPtr>::iterator findEquivalentVariable(const VariablePtr &equivalentVariable);
    std::vector<VariableWeakPtr>::const_iterator findEquivalentVariable(const VariablePtr &equivalentVariable) const;

};

}
