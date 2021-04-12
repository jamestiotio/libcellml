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

#include "libcellml/validator.h"

#include <algorithm>
#include <cmath>
#include <libxml/uri.h>
#include <map>
#include <set>
#include <stdexcept>

#include "libcellml/component.h"
#include "libcellml/importer.h"
#include "libcellml/importsource.h"
#include "libcellml/model.h"
#include "libcellml/reset.h"
#include "libcellml/units.h"
#include "libcellml/variable.h"

#include "namespaces.h"
#include "utilities.h"
#include "xmldoc.h"
#include "xmlutils.h"

#include "debug.h"

namespace libcellml {

/**
 * Type definition for a list of issue descriptions.
 */
using IssuesList = std::vector<std::vector<std::string>>;

/**
* @brief Validate that equivalent variable pairs in the @p model
* have equivalent units.
* Any errors will be logged in the @c Validator.
*
* Any difference in base units is reported as an error in the @c Validator, but the multiplier difference does not trigger a validator error.
* Where the base units are equivalent, the multiplier may be interpreted as units_of_v1 = (10^multiplier)*units_of_v2
*
* @param model The model containing the variables.
* @param v1 The variable which may contain units.
* @param v2 The equivalent variable which may contain units.
* @param hints String containing error messages to be passed back to the calling function for logging.
* @param multiplier Double returning the effective multiplier mismatch between the units.
*/
bool unitsAreEquivalent(const ModelPtr &model, const VariablePtr &v1, const VariablePtr &v2, std::string &hints, double &multiplier);

/**
* @brief Utility function used by unitsAreEquivalent to compare base units of two variables.
*
* @param model The model containing the variables.
* @param unitMap A list of the exponents of base variables.
* @param uName String name of the current variable being investigated.
* @param standardList Nested map of the conversion between built-in units and the base units they contain.
* @param uExp Exponent of the current unit in its parent.
* @param direction Specify whether we want to increment (1) or decrement (-1).
*/
void updateBaseUnitCount(const ModelPtr &model,
                         std::map<std::string, double> &unitMap,
                         double &multiplier,
                         const std::string &uName,
                         double uExp, double logMult, int direction);

/**
 * @brief Utility function to construct an @c Issue if required for a given CellML identifier string.
 *
 * Utility function to construct an @c Issue with the appropriate description for
 * a given CellML identifier string.
 *
 * @param name The @c std::string identifier to check.
 *
 * @return An @c IssuePtr if the @p name is illegal, @c nullptr otherwise.
 */
IssuePtr makeIssueIllegalIdentifier(const std::string &name);

/**
 * @brief Check if the provided @p name is a valid CellML identifier.
 *
 * Test if the given name is a valid CellML identifier according to
 * the CellML 2.0 specification.
 *
 * @param name The @c std::string identifier to check.
 *
 * @return @c true if @name is a valid CellML identifier and @c false otherwise.
 */
bool isCellmlIdentifier(const std::string &name);

/**
 * @brief isValidW3IdName
 * @param name
 * @return
 */
bool isValidW3IdName(const std::string &name);

/**
 * @brief Validate the provided @p name is a valid CellML identifier.
 *
 * Checks if the provided @p name is a valid CellML identifier according
 * to the CellML 2.0 specification. This requires a non-zero length Unicode
 * character sequence containing basic Latin alphanumeric characters or
 * underscores that does not begin with a number.  Returns the rule in the
 * specification that the given @p name violates.  If the name is a valid
 * name then Issue::ReferenceRule::UNDEFINED is returned.
 *
 * @param name The @c std::string identifier to check.
 *
 * @return Returns UNDEFINED if the name is a valid CellML identifier
 * otherwise one of:
 *  - DATA_REPR_IDENTIFIER_BEGIN_EURO_NUM;
 *  - DATA_REPR_IDENTIFIER_LATIN_ALPHANUM; or
 *  - DATA_REPR_IDENTIFIER_AT_LEAST_ONE_ALPHANUM.
 */
Issue::ReferenceRule validateCellmlIdentifier(const std::string &name);

/**
 * @brief The Validator::ValidatorImpl struct.
 *
 * The private implementation for the Validator class.
 */
struct Validator::ValidatorImpl
{
    Validator *mValidator = nullptr;

    /**
     * @brief Validate the given name is unique in the model.
     *
     * The @p name is checked against known names in @p names. If
     * the @p name already exists an issue is added to the validator
     * with the model passed to the issue for further reference.
     *
     * @param model The model the name is used in.
     * @param name The name of the component to validate.
     * @param names The list of component names already used in the model.
     */
    void validateUniqueName(const ModelPtr &model, const std::string &name, std::vector<std::string> &names) const;

    /**
     * @brief Validate the @p component using the CellML 2.0 Specification.
     *
     * Validate the given @p component and its encapsulated entities using
     * the CellML 2.0 Specification. Any issues will be logged in the @c Validator.
     *
     * @param component The component to validate.
     */
    void validateComponent(const ComponentPtr &component, HistoryList &history, std::vector<ModelPtr> &visitedModels) const;

    /**
     * @brief Validate the component tree of the given @p component.
     *
     * Validate the given compoment and all child components of the component.
     *
     * @param model The model the @p component comes from.
     * @param component The @c Component to validate.
     * @param componentNames The list of already used component names used
     * to track repeated component names.
     */
    void validateComponentTree(const ModelPtr &model, const ComponentPtr &component, std::vector<std::string> &componentNames, HistoryList &history, std::vector<ModelPtr> &visitedModels);

    /**
     * @brief Validate the @p units using the CellML 2.0 Specification.
     *
     * Validate the given @p units and its encapsulated entities using
     * the CellML 2.0 Specification. Any issues will be logged in the @c Validator.
     *
     * @param units The units to validate.
     * @param history The history of units visited.
     * @param issueList An array of loops, returned so that the reported issues are not too repetitive.
     */
    void validateUnits(const UnitsPtr &units, HistoryList &history, std::vector<ModelPtr> modelsVisited) const;

    /**
     * @brief Validate the variable connections in the @p model using the CellML 2.0 Specification.
     *
     * Validate the variable connections in the given @p model using
     * the CellML 2.0 Specification. Any issues will be logged in the @c Validator.
     *
     * @param model The model which may contain variable connections to validate.
     */
    void validateConnections(const ModelPtr &model) const;

    /**
     * @brief Validate the units of the given variables equivalent variables.
     *
     * Validate that the variables that are equivalent to the given variable all
     * have compatible units.
     *
     * @param model The model for which the variable and model belong.
     * @param variable The variable to validate.
     * @param alreadyReported A list of variable pointer pairs.
     */
    void validateEquivalenceUnits(const ModelPtr &model, const VariablePtr &variable, VariableMap &alreadyReported) const;

    /**
     * @brief Validate the structure of the variables equivalences.
     *
     * Validate the structure of the variables equivalences.
     *
     * @param variable The variable to validate.
     */
    void validateEquivalenceStructure(const VariablePtr &variable) const;

    /**
     * @brief Validate the variable interface type.
     *
     * Validate the interface type for the given variable.
     *
     * @param variable The variable to validate.
     * @param alreadyReported A list of variable pointer pairs.
     */
    void validateVariableInterface(const VariablePtr &variable, VariableMap &alreadyReported) const;

    /**
     * @brief Validate the @c unit at index @c index from @p units using the CellML 2.0 Specification.
     *
     * Validate the @c unit at index @c index from @p units using
     * the CellML 2.0 Specification. Any issues will be logged in the @c Validator.
     *
     * @param index The index of the @c unit to validate from @p units.
     * @param units The units to validate.
     */
    void validateUnitsUnit(size_t index, const UnitsPtr &units, HistoryList &history, std::vector<ModelPtr> modelsVisited) const;

    /**
     * @brief Validate the @p variable using the CellML 2.0 Specification.
     *
     * Validate the given @p variable using the CellML 2.0 Specification.
     * Any issues will be logged in the @c Validator.
     *
     * @param variable The variable to validate.
     * @param variableNames A vector list of the name attributes of the @p variable and its siblings.
     */
    void validateVariable(const VariablePtr &variable, const std::vector<std::string> &variableNames) const;

    /**
     * @brief Validate the @p reset using the CellML 2.0 Specification.
     *
     * Examine the @p reset for conformance to the CellML 2.0 specification.  Any
     * issues will be logged in the @c Validator.
     *
     * @param reset The reset to validate.
     * @param component The component the reset belongs to.
     */
    void validateReset(const ResetPtr &reset, const ComponentPtr &component) const;

    /**
     * @brief Validate the math @p input @c std::string.
     *
     * Validate the math @p input @c std::string using the CellML 2.0 Specification and
     * the W3C MathML DTD. Any issues will be logged in the @c Validator.
     *
     * @param input The math @c std::string to validate.
     * @param component The component containing the math @c std::string to be validated.
     */
    void validateMath(const std::string &input, const ComponentPtr &component) const;

    /**
     * @brief Traverse the node tree for invalid MathML elements.
     *
     * Traverse the Xml node tree checking that all MathML elements are listed in the
     * supported MathML elements table from the CellML specification 2.0 document.
     *
     * @param node The node to check children and sibling nodes.
     * @param component The component the MathML belongs to.
     */
    void validateMathMLElements(const XmlNodePtr &node, const ComponentPtr &component) const;

    /**
     * @brief Validate and clean the @cn node.
     *
     * Validate the @c cn node and clear any CellML namespace from the node.
     *
     * @param node The node @c cn element.
     * @param component The component the @p node is a part of.
     */
    void validateAndCleanCnNode(const XmlNodePtr &node, const ComponentPtr &component) const;

    /**
     * @brief Validate that the @c ci node has a reference to a variable.
     *
     * Validate that the @c ci node has a reference to a variable.
     *
     * @param node The node @c ci element from the document.
     * @param component The component the @p node is a part of.
     * @param variableNames A list of variable names.
     */
    void validateAndCleanCiNode(const XmlNodePtr &node, const ComponentPtr &component, const std::vector<std::string> &variableNames) const;

    /**
     * @brief Validate the text of a @c cn element.
     *
     * Validates that the @c cn element has a units attached.
     *
     * @param component The component that the @c cn element belongs to.
     * @param unitsName The name of the units.
     * @param textNode The text of the @c cn element.
     *
     * @return  @c true if the @c cn units is valid, @c false otherwise.
     */
    bool validateCnUnits(const ComponentPtr &component, const std::string &unitsName, const std::string &textNode) const;

    /**
     * @brief Validate CellML variables and units in MathML @c ci and @c cn variables. Removes CellML units from the @p node.
     *
     * Validates CellML variables found in MathML @c ci elements. Validates @c cellml:units
     * attributes found on @c cn elements and removes them from the @c XmlNode @p node to leave MathML that may then
     * be validated using the MathML DTD.
     *
     * @param node The @c XmlNode to validate CellML entities on and remove @c cellml:units from.
     * @param component The component that the math @c XmlNode @p node is contained within.
     * @param variableNames A @c vector list of the names of variables found within the @p component.
     */
    void validateAndCleanMathCiCnNodes(XmlNodePtr &node, const ComponentPtr &component, const std::vector<std::string> &variableNames) const;

    /**
     * @brief Check if the provided @p node is a supported MathML element.
     *
     * Checks if the provided @p node is one of the supported MathML elements defined in the table
     * of supported MathML elements from the CellML specification version 2.0 document.
     *
     * @param node The @c XmlNode node to check against the list of supported MathML elements.
     * @return @c true if @node is a supported MathML element and @c false otherwise.
     */
    bool isSupportedMathMLElement(const XmlNodePtr &node) const;

    /**
    * @brief Utility function called recursively by validateNoUnitsAreCyclic.
    *
    * @param model The model containing the units to be tested.
    * @param parent The current @c Units pointer to test.
    * @param history A vector of the chained dependencies. Cyclic variables exist where the first and last units are equal.
    * @param errorList An array of loops, returned so that the reported issues are not too repetitive.
    */
    void checkUnitsForCycles(const ModelPtr &model, const UnitsPtr &parent,
                             std::vector<std::string> &history,
                             IssuesList &errorList) const;

    /** @brief Function to check IDs within the model scope are unique.
     *
     * @param model The model to be checked.
     */
    void checkUniqueIds(const ModelPtr &model);

    /** @brief Utility function to construct a map of identifiers used within the model.
     *
     * @param model The model to be checked.
     * @return An IdMap of the items in the model with identifier fields.
     */
    IdMap buildModelIdMap(const ModelPtr &model);

    /** @brief Utility function called recursively to construct a map of identifiers in a component.
     *
     * @param component The component to check.
     * @param idMap The IdMap object to construct.
     * @param reportedConnections A set of connection identifiers to prevent duplicate reporting.
     */
    void buildComponentIdMap(const ComponentPtr &component, IdMap &idMap, std::set<std::string> &reportedConnections);

    /** @brief Utility function to add an item to the idMap.
     *
     * @param id A string identifier to add.
     * @param info A string description of the item with this identifier.
     * @param idMap The IdMap under construction.
     */
    void addIdMapItem(const std::string &id, const std::string &info, IdMap &idMap);

    /** @brief Utility function to parse MathML children and add element identifiers to idMap.
     *
     * @param node XMLNode to read.
     * @param component Owning component of the MathML string.
     * @param idMap The IdMap under construction.
     */
    void buildMathChildIdMap(const XmlNodePtr &node, const std::string &infoRef, IdMap &idMap);

    /** @brief Utility function to parse math and add element identifiers to idMap.
     *
     * @param component Component to investigate.
     * @param idMap The IdMap under construction.
     */
    void buildMathIdMap(const std::string &infoRef, IdMap &idMap, const std::string &input);

    /**
     * @brief Validate the import source xlink:href and id.
     *
     * @param importSource The import source to validate.
     * @param importName The name of the entity that the import is called.
     * @param importType The type of the entity the import is, either 'units', or 'component'.
     */
    void validateImportSource(const ImportSourcePtr &importSource, const std::string &importName, const std::string &importType) const;

    bool checkCycleForDuplications(std::vector<std::string> names) const;

    bool checkIssuesForDuplications(const std::string &description) const;

    std::string descriptionOriginMarker(size_t depth) const;
    std::string originText(size_t depth, const std::string &type, const std::string &name, const std::string &importSourceUrl) const;
};

bool checkForCycles(const HistoryList &history, const HistoryEntry &h)
{
    return std::find(history.begin(), history.end(), h) != history.end();
}

Validator::Validator()
    : mPimpl(new ValidatorImpl())
{
    mPimpl->mValidator = this;
}

Validator::~Validator()
{
    delete mPimpl;
}

ValidatorPtr Validator::create() noexcept
{
    return std::shared_ptr<Validator> {new Validator {}};
}

void Validator::validateModel(const ModelPtr &model)
{
    // Clear any pre-existing issues in ths validator instance.
    removeAllIssues();

    if (model == nullptr) {
        auto issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::INVALID_ARGUMENT);
        issue->setDescription("The model is null.");
        addIssue(issue);
    } else {
        // Check for a valid name attribute.
        if (!isCellmlIdentifier(model->name())) {
            auto issue = makeIssueIllegalIdentifier(model->name());
            issue->setModel(model);
            issue->setReferenceRule(Issue::ReferenceRule::MODEL_NAME);
            issue->setDescription("Model '" + model->name() + "' does not have a valid name attribute. " + issue->description());
            addIssue(issue);
        }
        // Check for a valid identifier.
        if (!isValidW3IdName(model->id())) {
            IssuePtr issue = Issue::create();
            issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
            issue->setModel(model);
            issue->setDescription("Model '" + model->name() + "' does not have a valid 'id' attribute, '" + model->id() + "'.");
            addIssue(issue);
        }
        std::vector<ModelPtr> modelsVisited = {model};
        // Check for components in this model.
        if (model->componentCount() > 0) {
            std::vector<std::string> componentNames;
            HistoryList history;
            for (size_t i = 0; i < model->componentCount(); ++i) {
                ComponentPtr component = model->component(i);
                mPimpl->validateComponentTree(model, component, componentNames, history, modelsVisited);
            }
        }
        // Check for units in this model.
        if (model->unitsCount() > 0) {
            HistoryList history;
            for (size_t i = 0; i < model->unitsCount(); ++i) {
                UnitsPtr units = model->units(i);
                auto h = std::make_tuple(units->name(), std::string(), std::string());
                if (std::find(history.begin(), history.end(), h) == history.end()) {
                    mPimpl->validateUnits(units, history, modelsVisited);
                    history.clear();
                }
//                std::vector<std::string> history;

//                history.push_back(unitsName);
//                mPimpl->checkUnitsForCycles(model, units, history, issueList);
//                unitsNames.push_back(units->name());
//                if (units->isImport()) {
//                    unitsImportSources.push_back(units->importSource()->url());
//                    unitsRefs.push_back(units->importReference());
//                }
            }

//            if (!issueList.empty()) {
//                std::vector<std::map<std::string, bool>> reportedIssueList;
//                for (auto &issues : issueList) {
//                    std::map<std::string, bool> hash;

//                    for (auto &e : issues) {
//                        hash.insert(std::pair<std::string, bool>(e, true));
//                    }

//                    // Only return as issue if this combo has not been reported already.
//                    if (std::find(reportedIssueList.begin(), reportedIssueList.end(), hash) == reportedIssueList.end()) {
//                        IssuePtr issue = Issue::create();
//                        std::string des = "'";
//                        for (size_t j = 0; j < issues.size() - 1; ++j) {
//                            des += issues[j] + "' -> '";
//                        }
//                        des += issues[issues.size() - 1] + "'";
//                        issue->setDescription("Cyclic units exist: " + des);
//                        auto cyclicUnits = model->units(issues[issues.size() - 1]);
//                        issue->setUnits(cyclicUnits);
//                        issue->setReferenceRule(Issue::ReferenceRule::UNIT_CIRCULAR_REF);
//                        addIssue(issue);
//                        reportedIssueList.push_back(hash);
//                    }
//                }
//            }
        }

        // Check that unit relationships are not cyclical.
//        if (model->unitsCount() > 0) {
//            mPimpl->validateNoUnitsAreCyclic(model);
//        }

        // Validate any connections / variable equivalence networks in the model.
        mPimpl->validateConnections(model);

        // Check identifiers across the model are unique.
        mPimpl->checkUniqueIds(model);
    }
}

void Validator::ValidatorImpl::validateUniqueName(const ModelPtr &model, const std::string &name, std::vector<std::string> &names) const
{
    if (!name.empty()) {
        if (std::find(names.begin(), names.end(), name) != names.end()) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Model '" + model->name() + "' contains multiple components with the name '" + name + "'. Valid component names must be unique to their model.");
            issue->setModel(model);
            issue->setReferenceRule(Issue::ReferenceRule::COMPONENT_NAME_UNIQUE);
            mValidator->addIssue(issue);
        } else {
            names.push_back(name);
        }
    }
}

void Validator::ValidatorImpl::validateComponentTree(const ModelPtr &model, const ComponentPtr &component, std::vector<std::string> &componentNames, HistoryList &history, std::vector<ModelPtr> &visitedModels)
{
    validateUniqueName(model, component->name(), componentNames);
    for (size_t i = 0; i < component->componentCount(); ++i) {
        auto childComponent = component->component(i);
        validateComponentTree(model, childComponent, componentNames, history, visitedModels);
    }
    validateComponent(component, history, visitedModels);
}

void Validator::ValidatorImpl::validateImportSource(const ImportSourcePtr &importSource, const std::string &importName, const std::string &importType) const
{
    std::string url = importSource->url();

    // Check for a valid identifier.
    if (!isValidW3IdName(importSource->id())) {
        IssuePtr issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
        issue->setImportSource(importSource);
        issue->setDescription("Import of " + importType + " '" + importName + "' does not have a valid 'id' attribute, '" + importSource->id() + "'.");
        mValidator->addIssue(issue);
    }

    if (url.empty()) {
        IssuePtr issue = Issue::create();
        issue->setDescription("Import of " + importType + " '" + importName + "' does not have a valid locator xlink:href attribute.");
        issue->setImportSource(importSource);
        issue->setReferenceRule(Issue::ReferenceRule::IMPORT_HREF);
        mValidator->addIssue(issue);
    } else {
        xmlURIPtr uri = xmlParseURI(url.c_str());
        if (uri == nullptr) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Import of " + importType + " '" + importName + "' has an invalid URI in the xlink:href attribute.");
            issue->setImportSource(importSource);
            issue->setReferenceRule(Issue::ReferenceRule::IMPORT_HREF);
            mValidator->addIssue(issue);

        } else {
            xmlFreeURI(uri);
        }
    }
}

void Validator::ValidatorImpl::validateComponent(const ComponentPtr &component, HistoryList &history, std::vector<ModelPtr> &visitedModels) const
{
    // Check for a valid name attribute.
    bool isImported = component->isImport();
    std::string descriptionPrefix = "Component ";
    if (isImported) {
        descriptionPrefix = "Imported component ";
    }
    bool isOrigingModel = visitedModels.size() == 1;
    if (!isOrigingModel) {
        Debug() << "This is not the origin model!!!";
    }
    if (!isCellmlIdentifier(component->name())) {
        auto issue = makeIssueIllegalIdentifier(component->name());
        issue->setComponent(component);
        issue->setDescription(descriptionPrefix + "'" + component->name() + "' does not have a valid name attribute. " + issue->description());
        issue->setReferenceRule(Issue::ReferenceRule::COMPONENT_NAME);
        mValidator->addIssue(issue);
    }
    // Check for a valid identifier.
    if (!isValidW3IdName(component->id())) {
        IssuePtr issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
        issue->setComponent(component);
        issue->setDescription(descriptionPrefix + "'" + component->name() + "' does not have a valid 'id' attribute, '" + component->id() + "'.");
        mValidator->addIssue(issue);
    }

    if (isImported) {
        // Check for a component_ref; assumes imported if the import source is not null.
        std::string componentRef = component->importReference();

        if (!isCellmlIdentifier(componentRef)) {
            auto issue = makeIssueIllegalIdentifier(componentRef);
            issue->setDescription(descriptionPrefix + "'" + component->name() + "' does not have a valid component_ref attribute. " + issue->description());
            issue->setComponentRef(component);
            issue->setReferenceRule(Issue::ReferenceRule::IMPORT_COMPONENT_COMPONENT_REF);
            mValidator->addIssue(issue);
        }

        validateImportSource(component->importSource(), component->name(), "component");

        auto importModel = component->importSource()->model();
        if (importModel != nullptr) {
            auto importedComponent = importModel->component(componentRef);
            if (importedComponent != nullptr) {
                auto h = createHistoryEntry(component);
                if (checkForCycles(history, h)) {
//                    history.push_back(h);
                    auto issue = makeIssueCyclicDependency(visitedModels.front(), "component", history, "resolve");
                    issue->setReferenceRule(Issue::ReferenceRule::IMPORT_COMPONENT_COMPONENT_REF);
                    issue->setImportSource(component->importSource());
                    mValidator->addIssue(issue);
                } else {
                    history.push_back(h);
                    visitedModels.push_back(importModel);
                    validateComponent(importedComponent, history, visitedModels);
                    visitedModels.pop_back();
                    history.pop_back();
                }
//                if (std::find(visitedModels.begin(), visitedModels.end(), importModel) != visitedModels.end()) {
//                    auto issue = makeIssueCyclicDependency(visitedModels.front(), "component", history, "resolve");
//                    issue->setReferenceRule(Issue::ReferenceRule::IMPORT_COMPONENT_COMPONENT_REF);
//                    issue->setImportSource(component->importSource());
//                    mValidator->addIssue(issue);
//                } else {
//                    visitedModels.push_back(importModel);
//                    validateComponent(importedComponent, history, visitedModels);
//                    visitedModels.pop_back();
//                }
            } else {
                auto issue = Issue::create();
                issue->setDescription(descriptionPrefix + "'" + component->name() + "' refers to component '" + componentRef + "' which does not appear in '" + importModel->name() + "'.");
                issue->setComponent(component);
                issue->setReferenceRule(Issue::ReferenceRule::IMPORT_COMPONENT_COMPONENT_REF);
                mValidator->addIssue(issue);
            }
        }

    } else {
        // Check for variables in this component.
        std::vector<std::string> variableNames;
        // Validate variable(s).
        for (size_t i = 0; i < component->variableCount(); ++i) {
            VariablePtr variable = component->variable(i);
            validateVariable(variable, variableNames);
            variableNames.push_back(variable->name());
        }
        // Check for resets in this component
        for (size_t i = 0; i < component->resetCount(); ++i) {
            ResetPtr reset = component->reset(i);
            validateReset(reset, component);
        }

        // Validate math through the private implementation (for XML handling).
        if (!component->math().empty()) {
            validateMath(component->math(), component);
        }
    }
}

std::vector<std::string> split(const std::string &content, const std::string &delimiter = ";")
{
    std::vector<std::string> strings;
    std::size_t current, previous = 0;
    current = content.find(delimiter);
    while (current != std::string::npos) {
        strings.push_back(content.substr(previous, current - previous));
        previous = current + delimiter.size();
        current = content.find(delimiter, previous);
    }
    strings.push_back(content.substr(previous, current - previous));

    return strings;
}

std::set<std::string> namesInCycle(std::vector<std::string> allNames)
{
    std::string cycleStartName = allNames.back();
    allNames.pop_back();
    std::reverse(allNames.begin(), allNames.end());
    std::set<std::string> namesInCycle = {cycleStartName};
    for (const auto &name : allNames) {
        if (name == cycleStartName) {
            break;
        }
        namesInCycle.emplace(name);
    }

    return namesInCycle;
}

bool Validator::ValidatorImpl::checkCycleForDuplications(std::vector<std::string> names) const
{
    std::set<std::string> testNamesInCycle = namesInCycle(names);
    bool found = false;
    for(size_t i = 0; i < mValidator->issueCount() && !found; ++i) {
        auto issue = mValidator->issue(i);
        if (issue->description().substr(0, 20) == "Cyclic units exist: ") {
            auto loop = issue->description().substr(20);
            auto parts = split(loop, " -> ");
            auto existingNamesInCycle =  namesInCycle(parts);
            found = testNamesInCycle == existingNamesInCycle;
        }
    }

    return found;
}

std::string Validator::ValidatorImpl::descriptionOriginMarker(size_t depth) const
{
    std::string originMarker = "";
    if (depth > 1) {
        originMarker = "NOT ORIGIN: ";
    }

    return originMarker;
}

bool Validator::ValidatorImpl::checkIssuesForDuplications(const std::string &description) const
{
    size_t count = mValidator->issueCount();
    for (size_t i = 0; i < count; ++i) {
        if (mValidator->issue(count - 1 - i)->description() == description) {
            return true;
        }
    }
    return false;
}

std::string Validator::ValidatorImpl::originText(size_t depth, const std::string &type, const std::string &name, const std::string &importSourceUrl) const
{
    std::string originMarker = "";
    if (depth > 1) {
//        Debug() << "======= " << std::get<0>(entry) << std::get<1>(entry) << std::get<2>(entry);
        originMarker = " - " + type + " '" + name + "' imported from '" + importSourceUrl+ "' has error; ";
    }

    return originMarker;
}

void Validator::ValidatorImpl::validateUnits(const UnitsPtr &units, HistoryList &history, std::vector<ModelPtr> modelsVisited) const
{
    auto h = createHistoryEntry(units);
    if (checkForCycles(history, h)) {
        history.push_back(h);
        std::string des;
        std::vector<std::string> names;
        for (const auto &entry : history) {
            if (!des.empty()) {
                des += " -> ";
            }
            auto tmp = "'" + std::get<0>(entry) + "'";
            names.push_back(tmp);
            des += tmp;
        }
        if (!checkCycleForDuplications(names)) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Cyclic units exist: " + des);
            issue->setUnits(units);
            issue->setReferenceRule(Issue::ReferenceRule::UNIT_CIRCULAR_REF);
            mValidator->addIssue(issue);
        }
        history.pop_back();
        return;
    }

    std::string unitsName = units->name();
    size_t initialIssueCount = mValidator->issueCount();
    bool isOriginModel = modelsVisited.size() == 1;
    std::string importSourceUrl = "";
    if (history.size() > 1) {
        importSourceUrl = std::get<2>(history.at(history.size() - 2));
    }
    std::string descriptionMarker = descriptionOriginMarker(modelsVisited.size());
    std::string originMarker = originText(modelsVisited.size(), "Units", unitsName, importSourceUrl);
    std::string descriptionPrefix = descriptionMarker + originMarker;

    history.push_back(h);
//    printHistory(history);
    auto model = owningModel(units);

    std::string unitsRef;
    std::string unitsImportUrl;
    if (units->isImport()) {
        unitsRef = units->importReference();
        unitsImportUrl = units->importSource()->url();
    }

    size_t unitsWithNameCount = 0;
    size_t unitsWithImportSource = 0;
    Debug() << unitsName << " - " << modelsVisited.size();
    for (size_t i = 0; i < model->unitsCount() && modelsVisited.size() == 1; ++i) {
        auto tmpUnits = model->units(i);
        if (tmpUnits->name() == unitsName) {
            unitsWithNameCount += 1;
        }
        if (tmpUnits->isImport()) {
            if (tmpUnits->importReference() == unitsRef && tmpUnits->importSource()->url() == unitsImportUrl) {
                unitsWithImportSource += 1;
            }
        }
    }

    if (units->isImport()) {
        // Check for a units_ref.
        size_t currentIssueCount = mValidator->issueCount();
        std::string unitsRef = units->importReference();
        if (!isCellmlIdentifier(unitsRef)) {
            auto issue = makeIssueIllegalIdentifier(unitsRef);
            issue->setDescription(descriptionPrefix + "Imported units '" + unitsName + "' does not have a valid units_ref attribute. " + issue->description());
            issue->setUnits(units);
            issue->setReferenceRule(Issue::ReferenceRule::IMPORT_UNITS_REF);
            mValidator->addIssue(issue);
        }

        auto importSource = units->importSource();
        validateImportSource(importSource, unitsName , "units");

        bool foundImportIssue = mValidator->issueCount() > currentIssueCount;
//        std::string url = importSource->url();

        // Check if we already have another import from the same source with the same units_ref.
        // (This looks for matching entries at the same position in the source and ref vectors).
        if ((unitsWithImportSource > 1) && !foundImportIssue) {
            auto description = descriptionPrefix + "Model '" + model->name() + "' contains multiple imported units from '" + unitsImportUrl + "' with the same units_ref attribute '" + unitsRef + "'.";
            if (!checkIssuesForDuplications(description)) {
                IssuePtr issue = Issue::create();
                issue->setDescription(description);
                issue->setModel(model);
                issue->setReferenceRule(Issue::ReferenceRule::IMPORT_UNITS_REF);
                mValidator->addIssue(issue);
            }
        }

        auto importSourceModel = importSource->model();
        if (importSourceModel != nullptr) {
            auto importedUnits = importSourceModel->units(unitsRef);
            if (importedUnits != nullptr) {
                Strings importUnitsNames;
                Strings importUnitsRefs;
                Strings importUnitsImportSources;
                IssuesList importIssueList;

                if (std::find(modelsVisited.begin(), modelsVisited.end(), importSourceModel) != modelsVisited.end()) {
                    auto issue = makeIssueCyclicDependency(modelsVisited.front(), "units", history, "resolve");
//                    auto issue = Issue::create();
//                    issue->setDescription(descriptionPrefix + "Loopy loop: '" + units->name() + "' refers to units '" + unitsRef + "' which does not appear in '" + importSourceModel->name() + "'.");
                    issue->setUnits(units);
                    issue->setReferenceRule(Issue::ReferenceRule::IMPORT_UNITS_REF);
                    mValidator->addIssue(issue);
                } else {
                    bool sourceModel = true;
                    if (modelsVisited.size() > 1) {
                        sourceModel = false;
                    }
                    size_t currentIssueCount = mValidator->issueCount();
                    Debug() << "validate units entry: " << sourceModel << " - " << modelsVisited.size() << " = " << mValidator->issueCount();
                    modelsVisited.push_back(importSourceModel);

                    validateUnits(importedUnits, history, modelsVisited);
                    modelsVisited.pop_back();
                    Debug() << "validate units exit : " << sourceModel << " - " << modelsVisited.size() << " = " << mValidator->issueCount();
                    if (mValidator->issueCount() > currentIssueCount) {
                        Debug() << mValidator->issue(mValidator->issueCount() - 1)->description();
                    }

                }
            } else {
                auto issue = Issue::create();
                issue->setDescription(descriptionPrefix + "Imported units '" + units->name() + "' refers to units '" + unitsRef + "' which does not appear in '" + importSourceModel->name() + "'.");
                issue->setUnits(units);
                issue->setReferenceRule(Issue::ReferenceRule::IMPORT_UNITS_REF);
                mValidator->addIssue(issue);
            }
        }

    }
    // Check for duplicate units names in this model.
    if (unitsWithNameCount > 1) {
        std::string description = descriptionPrefix + "Model '" + model->name() + "' contains multiple units with the name '" + unitsName + "'. Valid units names must be unique to their model.";
        if (!checkIssuesForDuplications(description)) {
            IssuePtr issue = Issue::create();
            issue->setDescription(description);
            issue->setModel(model);
            issue->setReferenceRule(Issue::ReferenceRule::UNITS_NAME_UNIQUE);
            mValidator->addIssue(issue);
        }
    }
    // Check for a valid name attribute.
    if (!isCellmlIdentifier(unitsName)) {
        auto issue = makeIssueIllegalIdentifier(unitsName);
        issue->setUnits(units);
        if (units->isImport()) {
            issue->setDescription(descriptionPrefix + "Imported units '" + unitsName + "' does not have a valid name attribute. " + issue->description());
            issue->setReferenceRule(Issue::ReferenceRule::IMPORT_UNITS_NAME);
        } else {
            issue->setDescription(descriptionPrefix + "Units '" + unitsName + "' does not have a valid name attribute. " + issue->description());
            issue->setReferenceRule(Issue::ReferenceRule::UNITS_NAME);
        }
        mValidator->addIssue(issue);
    } else {
        // Check for a matching standard units.
        if (isStandardUnitName(unitsName)) {
            IssuePtr issue = Issue::create();
            issue->setDescription(descriptionPrefix + "Units is named '" + unitsName + "' which is a protected standard unit name.");
            issue->setUnits(units);
            issue->setReferenceRule(Issue::ReferenceRule::UNITS_STANDARD);
            mValidator->addIssue(issue);
        }
    }
    // Check for a valid identifier.
    if (!isValidW3IdName(units->id())) {
        IssuePtr issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
        issue->setUnits(units);
        std::string descriptionStart = "Units";
        if (units->isImport()) {
            descriptionStart = "Imported units";
        }
        issue->setDescription(descriptionPrefix + descriptionStart + " '" + unitsName + "' does not have a valid 'id' attribute, '" + units->id() + "'.");
        mValidator->addIssue(issue);
    }
    if (units->unitCount() > 0) {
        for (size_t i = 0; i < units->unitCount(); ++i) {
            validateUnitsUnit(i, units, history, modelsVisited);
        }
    }

    for (size_t i = initialIssueCount; i < mValidator->issueCount() && isOriginModel; ++i) {
        auto notOriginText = descriptionOriginMarker(size_t(2));
        auto issue = mValidator->issue(i);
        auto description = issue->description();
        if (description.substr(0, notOriginText.size()) == notOriginText) {
            Debug() << "Origin model issue: " << mValidator->issue(i)->description();
            description.replace(0, notOriginText.size(), "Imported units '" + units->name() + "' is not valid because:\n");
            issue->setDescription(description);
            issue->setUnits(units);
        }
    }
    history.pop_back();
}

void Validator::ValidatorImpl::validateUnitsUnit(size_t index, const UnitsPtr &units, HistoryList &history, std::vector<ModelPtr> modelsVisited) const
{
    std::string descriptionMarker = descriptionOriginMarker(modelsVisited.size());
    std::string importSourceUrl = "";
    if (history.size() > 1) {
        importSourceUrl = std::get<2>(history.at(history.size() - 2));
    }
    std::string originMarker = originText(modelsVisited.size(), "Units", units->name(), importSourceUrl);
    std::string descriptionPrefix = descriptionMarker + originMarker;

    // Validate the unit at the given index.
    std::string reference;
    std::string prefix;
    std::string id;
    double exponent;
    double multiplier;

    units->unitAttributes(index, reference, prefix, exponent, multiplier, id);
    if (isCellmlIdentifier(reference)) {
        ModelPtr model = owningModel(units);
        if (model->hasUnits(reference) && (!isStandardUnitName(reference))) {
            Debug() << "Wanna go from here? " << reference;
            validateUnits(model->units(reference), history, modelsVisited);
        } else if (!model->hasUnits(reference) && (!isStandardUnitName(reference))) {
            IssuePtr issue = Issue::create();
            issue->setDescription(descriptionPrefix + "Units reference '" + reference + "' in units '" + units->name() + "' is not a valid reference to a local units or a standard unit type.");
            issue->setUnit(Unit::create(units, index));
            issue->setReferenceRule(Issue::ReferenceRule::UNIT_UNITS_REF);
            mValidator->addIssue(issue);
        }
    } else {
        auto issue = makeIssueIllegalIdentifier(reference);
        issue->setDescription(descriptionPrefix + "Unit in units '" + units->name() + "' does not have a valid units reference. The reference given is '" + reference + "'. " + issue->description());
        issue->setUnit(Unit::create(units, index));
        issue->setReferenceRule(Issue::ReferenceRule::UNIT_UNITS_REF);
        mValidator->addIssue(issue);
    }
    // Check for a valid identifier.
    if (!isValidW3IdName(id)) {
        IssuePtr issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
        issue->setUnit(Unit::create(units, index));
        issue->setDescription(descriptionPrefix + "Unit in units '" + units->name() + "' does not have a valid 'id' attribute, '" + units->id() + "'.");
        mValidator->addIssue(issue);
    }
    if (!prefix.empty()) {
        if (!isStandardPrefixName(prefix)) {
            if (!isCellMLInteger(prefix)) {
                IssuePtr issue = Issue::create();
                issue->setDescription(descriptionPrefix + "Prefix '" + prefix + "' of a unit referencing '" + reference + "' in units '" + units->name() + "' is not a valid integer or an SI prefix.");
                issue->setUnit(Unit::create(units, index));
                issue->setReferenceRule(Issue::ReferenceRule::UNIT_PREFIX);
                mValidator->addIssue(issue);
            } else {
                try {
                    int test = std::stoi(prefix);
                    (void)test;
                } catch (std::out_of_range &) {
                    IssuePtr issue = Issue::create();
                    issue->setDescription(descriptionPrefix + "Prefix '" + prefix + "' of a unit referencing '" + reference + "' in units '" + units->name() + "' is out of the integer range.");
                    issue->setUnit(Unit::create(units, index));
                    issue->setReferenceRule(Issue::ReferenceRule::UNIT_PREFIX);
                    mValidator->addIssue(issue);
                }
            }
        }
    }
}

void Validator::ValidatorImpl::validateVariable(const VariablePtr &variable, const std::vector<std::string> &variableNames) const
{
    ComponentPtr component = owningComponent(variable);
    auto variableName = variable->name();
    if (!variableName.empty()) {
        if (std::find(variableNames.begin(), variableNames.end(), variableName) != variableNames.end()) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Component '" + component->name() + "' contains multiple variables with the name '" + variableName + "'. Valid variable names must be unique to their component.");
            issue->setComponent(component);
            issue->setReferenceRule(Issue::ReferenceRule::VARIABLE_NAME);
            mValidator->addIssue(issue);
        }
    }

    // Check for a valid name attribute.
    if (!isCellmlIdentifier(variableName)) {
        auto issue = makeIssueIllegalIdentifier(variableName);
        issue->setDescription("Variable '" + variableName + "' in component '" + component->name() + "' does not have a valid name attribute. " + issue->description());
        issue->setVariable(variable);
        issue->setReferenceRule(Issue::ReferenceRule::VARIABLE_NAME);
        mValidator->addIssue(issue);
    }
    // Check for a valid identifier.
    if (!isValidW3IdName(variable->id())) {
        IssuePtr issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
        issue->setVariable(variable);
        issue->setDescription("Variable '" + variableName + "' does not have a valid 'id' attribute, '" + variable->id() + "'.");
        mValidator->addIssue(issue);
    }
    // Check for a valid units attribute.
    if (variable->units() == nullptr) {
        IssuePtr issue = Issue::create();
        issue->setDescription("Variable '" + variableName + "' in component '" + component->name() + "' does not have any units specified.");
        issue->setVariable(variable);
        issue->setReferenceRule(Issue::ReferenceRule::VARIABLE_UNITS);
        mValidator->addIssue(issue);
    } else {
        std::string unitsName = variable->units()->name();
        if (!isCellmlIdentifier(unitsName)) {
            auto issue = makeIssueIllegalIdentifier(unitsName);
            issue->setDescription("Variable '" + variableName + "' in component '" + component->name() + "' does not have a valid units attribute. The attribute given is '" + unitsName + "'. " + issue->description());
            issue->setVariable(variable);
            issue->setReferenceRule(Issue::ReferenceRule::VARIABLE_UNITS);
            mValidator->addIssue(issue);
        } else if (!isStandardUnitName(unitsName)) {
            ModelPtr model = owningModel(component);
            if ((model != nullptr) && !model->hasUnits(unitsName)) {
                IssuePtr issue = Issue::create();
                issue->setDescription("Variable '" + variableName + "' in component '" + component->name() + "' has a units reference '" + unitsName + "' which is neither standard nor defined in the parent model.");
                issue->setVariable(variable);
                issue->setReferenceRule(Issue::ReferenceRule::VARIABLE_UNITS);
                mValidator->addIssue(issue);
            }
        }
    }
    // Check for a valid interface attribute.
    if (!variable->interfaceType().empty()) {
        std::string interfaceType = variable->interfaceType();
        if ((interfaceType != "public") && (interfaceType != "private") && (interfaceType != "none") && (interfaceType != "public_and_private")) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Variable '" + variableName + "' in component '" + component->name() + "' has an invalid interface attribute value '" + interfaceType + "'.");
            issue->setVariable(variable);
            issue->setReferenceRule(Issue::ReferenceRule::VARIABLE_INTERFACE);
            mValidator->addIssue(issue);
        }
    }
    // Check for a valid initial value attribute.
    if (!variable->initialValue().empty()) {
        std::string initialValue = variable->initialValue();
        // Check if initial value is a variable reference
        if (!component->hasVariable(initialValue)) {
            // Otherwise, check that the initial value can be converted to a double
            if (!isCellMLReal(initialValue)) {
                IssuePtr issue = Issue::create();
                issue->setDescription("Variable '" + variableName + "' in component '" + component->name() + "' has an invalid initial value '" + initialValue + "'. Initial values must be a real number string or a variable reference.");
                issue->setVariable(variable);
                issue->setReferenceRule(Issue::ReferenceRule::VARIABLE_INITIAL_VALUE);
                mValidator->addIssue(issue);
            }
        }
    }
}

void Validator::ValidatorImpl::validateReset(const ResetPtr &reset, const ComponentPtr &component) const
{
    bool noOrder = false;
    bool noVariable = false;
    bool noTestVariable = false;
    bool noTestValue = false;
    bool noResetValue = false;
    bool varOutsideComponent = false;
    bool testVarOutsideComponent = false;
    std::string varParentName;
    std::string testVarParentName;

    std::string description = "Reset in component '" + component->name() + "' ";

    if (reset->isOrderSet()) {
        description += "with order '" + convertToString(reset->order()) + "', ";
    } else {
        noOrder = true;
    }

    // Check for a valid identifier.
    if (!isValidW3IdName(reset->id())) {
        IssuePtr issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
        issue->setReset(reset);
        issue->setDescription(description + "' does not have a valid 'id' attribute, '" + reset->id() + "'.");
        mValidator->addIssue(issue);
    }

    if (reset->variable() == nullptr) {
        noVariable = true;
    } else {
        description += "with variable '" + reset->variable()->name() + "', ";
        auto var = reset->variable();
        auto varParent = owningComponent(var);
        varParentName = varParent->name();
        if (varParentName != component->name()) {
            varOutsideComponent = true;
        }
    }

    if (reset->testVariable() == nullptr) {
        noTestVariable = true;
    } else {
        description += "with test_variable '" + reset->testVariable()->name() + "', ";

        auto var = reset->testVariable();
        auto varParent = owningComponent(var);
        testVarParentName = varParent->name();
        if (testVarParentName != component->name()) {
            testVarOutsideComponent = true;
        }
    }

    // Check that the contents of test_value and reset_value are valid MathML
    std::string testValueString = reset->testValue();
    std::string resetValueString = reset->resetValue();

    if ((testValueString.empty()) || (std::all_of(testValueString.begin(), testValueString.end(), isspace))) {
        noTestValue = true;
    } else {
        validateMath(testValueString, component);
    }
    if ((resetValueString.empty()) || (std::all_of(resetValueString.begin(), resetValueString.end(), isspace))) {
        noResetValue = true;
    } else {
        validateMath(resetValueString, component);
    }

    // Check for a valid identifier.
    if (!isValidW3IdName(reset->testValueId())) {
        IssuePtr issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
        issue->setReset(reset);
        issue->setDescription(description + "' does not have a valid test_value 'id' attribute, '" + reset->testValueId() + "'.");
        mValidator->addIssue(issue);
    }
    // Check for a valid identifier.
    if (!isValidW3IdName(reset->resetValueId())) {
        IssuePtr issue = Issue::create();
        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
        issue->setReset(reset);
        issue->setDescription(description + "' does not have a valid reset_value 'id' attribute, '" + reset->resetValueId() + "'.");
        mValidator->addIssue(issue);
    }

    if (noOrder) {
        IssuePtr issue = Issue::create();
        issue->setDescription(description + "does not have an order set.");
        issue->setComponent(component);
        issue->setReferenceRule(Issue::ReferenceRule::RESET_ORDER);
        mValidator->addIssue(issue);
    }
    if (noVariable) {
        IssuePtr issue = Issue::create();
        issue->setDescription(description + "does not reference a variable.");
        issue->setReset(reset);
        issue->setReferenceRule(Issue::ReferenceRule::RESET_VARIABLE_REF);
        mValidator->addIssue(issue);
    }
    if (noTestVariable) {
        IssuePtr issue = Issue::create();
        issue->setDescription(description + "does not reference a test_variable.");
        issue->setReset(reset);
        issue->setReferenceRule(Issue::ReferenceRule::RESET_TEST_VARIABLE_REF);
        mValidator->addIssue(issue);
    }
    if (noTestValue) {
        IssuePtr issue = Issue::create();
        issue->setDescription(description + "does not have a test_value specified.");
        issue->setReset(reset);
        issue->setReferenceRule(Issue::ReferenceRule::RESET_TEST_VALUE);
        mValidator->addIssue(issue);
    }
    if (noResetValue) {
        IssuePtr issue = Issue::create();
        issue->setDescription(description + "does not have a reset_value specified.");
        issue->setReset(reset);
        issue->setReferenceRule(Issue::ReferenceRule::RESET_RESET_VALUE);
        mValidator->addIssue(issue);
    }
    if (varOutsideComponent) {
        IssuePtr issue = Issue::create();
        issue->setDescription(description + "refers to a variable '" + reset->variable()->name() + "' in a different component '" + varParentName + "'.");
        issue->setReset(reset);
        issue->setReferenceRule(Issue::ReferenceRule::RESET_VARIABLE_REF);
        mValidator->addIssue(issue);
    }
    if (testVarOutsideComponent) {
        IssuePtr issue = Issue::create();
        issue->setDescription(description + "refers to a test_variable '" + reset->testVariable()->name() + "' in a different component '" + testVarParentName + "'.");
        issue->setReset(reset);
        issue->setReferenceRule(Issue::ReferenceRule::RESET_TEST_VARIABLE_REF);
        mValidator->addIssue(issue);
    }
}

void Validator::ValidatorImpl::validateMath(const std::string &input, const ComponentPtr &component) const
{
    // Parse as XML first.
    std::vector<XmlDocPtr> docs = multiRootXml(input);
    for (const auto &doc : docs) {
        // Copy any XML parsing issues into the common validator issue handler.
        if (doc->xmlErrorCount() > 0) {
            for (size_t i = 0; i < doc->xmlErrorCount(); ++i) {
                IssuePtr issue = Issue::create();
                issue->setDescription("LibXml2 error: " + doc->xmlError(i));
                issue->setReferenceRule(Issue::ReferenceRule::XML);
                mValidator->addIssue(issue);
            }
        }
        XmlNodePtr node = doc->rootNode();
        if (node == nullptr) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Could not get a valid XML root node from the math on component '" + component->name() + "'.");
            issue->setComponent(component);
            issue->setReferenceRule(Issue::ReferenceRule::XML);
            mValidator->addIssue(issue);
            return;
        }
        if (!node->isMathmlElement("math")) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Math root node is of invalid type '" + node->name() + "' on component '" + component->name() + "'. A valid math root node should be of type 'math'.");
            issue->setComponent(component);
            issue->setReferenceRule(Issue::ReferenceRule::XML);

            mValidator->addIssue(issue);
            return;
        }

        XmlNodePtr nodeCopy = node;
        std::vector<std::string> variableNames;
        for (size_t i = 0; i < component->variableCount(); ++i) {
            std::string variableName = component->variable(i)->name();
            if (std::find(variableNames.begin(), variableNames.end(), variableName) == variableNames.end()) {
                variableNames.push_back(variableName);
            }
        }

        validateMathMLElements(nodeCopy, component);

        // Iterate through ci/cn elements and remove cellml units attributes.
        XmlNodePtr mathNode = node;
        validateAndCleanMathCiCnNodes(node, component, variableNames);

        // Remove the cellml namespace definition.
        if (mathNode->hasNamespaceDefinition(CELLML_2_0_NS)) {
            mathNode->removeNamespaceDefinition(CELLML_2_0_NS);
        }

        // Get the MathML string with cellml:units attributes and namespace already removed.
        std::string cleanMathml = mathNode->convertToString();

        // Parse/validate the clean math string with the W3C MathML DTD.
        XmlDocPtr mathmlDoc = std::make_shared<XmlDoc>();
        mathmlDoc->parseMathML(cleanMathml);
        // Copy any MathML validation errors into the common validator error handler.
        if (mathmlDoc->xmlErrorCount() > 0) {
            for (size_t i = 0; i < mathmlDoc->xmlErrorCount(); ++i) {
                IssuePtr issue = Issue::create();
                issue->setDescription("W3C MathML DTD error: " + mathmlDoc->xmlError(i));
                issue->setMath(component);
                issue->setReferenceRule(Issue::ReferenceRule::MATH_MATHML);
                mValidator->addIssue(issue);
            }
        }
    }
}

bool Validator::ValidatorImpl::validateCnUnits(const ComponentPtr &component, const std::string &unitsName, const std::string &textNode) const
{
    if (isCellmlIdentifier(unitsName)) {
        return true;
    }

    IssuePtr issue = makeIssueIllegalIdentifier(unitsName);
    issue->setDescription("Math cn element with the value '" + textNode + "' does not have a valid cellml:units attribute. " + issue->description());
    issue->setMath(component);
    issue->setReferenceRule(Issue::ReferenceRule::MATH_CN_UNITS);
    mValidator->addIssue(issue);

    return false;
}

std::string text(const XmlNodePtr &node)
{
    if (node != nullptr) {
        if (node->isText()) {
            return node->convertToStrippedString();
        }
    }
    return {};
}

void Validator::ValidatorImpl::validateAndCleanCnNode(const XmlNodePtr &node, const ComponentPtr &component) const
{
    // Get cellml:units attribute.
    XmlAttributePtr attribute = node->firstAttribute();
    std::string unitsName;
    XmlAttributePtr unitsAttribute = nullptr;
    std::vector<XmlAttributePtr> cellmlAttributesToRemove;
    while (attribute) {
        if (!attribute->value().empty()) {
            if (attribute->isCellmlType("units")) {
                unitsName = attribute->value();
                unitsAttribute = attribute;
                cellmlAttributesToRemove.push_back(attribute);
            } else if (attribute->inNamespaceUri(CELLML_2_0_NS)) {
                cellmlAttributesToRemove.push_back(attribute);
                IssuePtr issue = Issue::create();
                issue->setDescription("Math " + node->name() + " element has an invalid attribute type '" + attribute->name() + "' in the cellml namespace. Attribute 'units' is the only CellML namespace attribute allowed.");
                issue->setMath(component);
                issue->setReferenceRule(Issue::ReferenceRule::MATH_MATHML);
                mValidator->addIssue(issue);
            }
        }
        attribute = attribute->next();
    }

    XmlNodePtr childNode = node->firstChild();
    std::string textInNode = text(childNode);
    // Check that cellml:units has been set.
    bool checkUnitsIsInModel = validateCnUnits(component, unitsName, textInNode);

    // Check that a specified units is valid.
    if (checkUnitsIsInModel) {
        // Check for a matching units in this model.
        ModelPtr model = owningModel(component);
        if (!model->hasUnits(unitsName)) {
            // Check for a matching standard units.
            if (!isStandardUnitName(unitsName)) {
                IssuePtr issue = Issue::create();
                issue->setDescription("Math has a " + node->name() + " element with a cellml:units attribute '" + unitsName + "' that is not a valid reference to units in the model '" + model->name() + "' or a standard unit.");
                issue->setMath(component);
                issue->setReferenceRule(Issue::ReferenceRule::MATH_CN_UNITS);
                mValidator->addIssue(issue);
            }
        }
    }
    // Now that we've validated this XML node's cellml:units attribute, remove it from the node.
    // This is done so we can validate a "clean" MathML string using the MathML DTD. The math
    // string stored on the component will not be affected.
    for (const auto &cellmlAttribute : cellmlAttributesToRemove) {
        cellmlAttribute->removeAttribute();
    }
    if (node->hasNamespaceDefinition(CELLML_2_0_NS)) {
        node->removeNamespaceDefinition(CELLML_2_0_NS);
    }
}

void Validator::ValidatorImpl::validateAndCleanCiNode(const XmlNodePtr &node, const ComponentPtr &component, const std::vector<std::string> &variableNames) const
{
    XmlNodePtr childNode = node->firstChild();
    std::string textInNode = text(childNode);
    if (!textInNode.empty()) {
        // Check whether we can find this text as a variable name in this component.
        if (std::find(variableNames.begin(), variableNames.end(), textInNode) == variableNames.end()) {
            IssuePtr issue = Issue::create();
            issue->setDescription("MathML ci element has the child text '" + textInNode + "' which does not correspond with any variable names present in component '" + component->name() + "'.");
            issue->setMath(component);
            issue->setReferenceRule(Issue::ReferenceRule::MATH_CI_VARIABLE_REF);
            mValidator->addIssue(issue);
        }
    }
}

void Validator::ValidatorImpl::validateAndCleanMathCiCnNodes(XmlNodePtr &node, const ComponentPtr &component, const std::vector<std::string> &variableNames) const
{
    if (node->isMathmlElement("cn")) {
        validateAndCleanCnNode(node, component);
    } else if (node->isMathmlElement("ci")) {
        validateAndCleanCiNode(node, component, variableNames);
    }
    // Check children for ci/cn.
    XmlNodePtr childNode = node->firstChild();
    if (childNode != nullptr) {
        validateAndCleanMathCiCnNodes(childNode, component, variableNames);
    }
    // Check siblings for ci/cn.
    node = node->next();
    if (node != nullptr) {
        validateAndCleanMathCiCnNodes(node, component, variableNames);
    }
}

void Validator::ValidatorImpl::validateMathMLElements(const XmlNodePtr &node, const ComponentPtr &component) const
{
    XmlNodePtr childNode = node->firstChild();
    if (childNode != nullptr) {
        if (!childNode->isComment() && !childNode->isText() && !isSupportedMathMLElement(childNode)) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Math has a '" + childNode->name() + "' element that is not a supported MathML element.");
            issue->setMath(component);
            issue->setReferenceRule(Issue::ReferenceRule::MATH_CHILD);
            mValidator->addIssue(issue);
        }
        validateMathMLElements(childNode, component);
    }

    XmlNodePtr nextNode = node->next();
    if (nextNode != nullptr) {
        if (!nextNode->isComment() && !nextNode->isText() && !isSupportedMathMLElement(nextNode)) {
            IssuePtr issue = Issue::create();
            issue->setDescription("Math has a '" + nextNode->name() + "' element that is not a supported MathML element.");
            issue->setMath(component);
            issue->setReferenceRule(Issue::ReferenceRule::MATH_CHILD);
            mValidator->addIssue(issue);
        }
        validateMathMLElements(nextNode, component);
    }
}

/**
 * @brief Test to see if the given variables are reachable from their parent components.
 *
 * Determine if the variables parents are reachable from one another.  That is they either
 * have a parent/child relationship or a sibling relationship in the model's component
 * hierarchy.
 *
 * Both variables must have a valid parent.
 *
 * @param variable1 The first variable.
 * @param variable2 The second variable.
 *
 * @return @c true if the parents of the given variables are reachable in the model component
 * hierarchy, @c false otherwise.
 */
bool reachableEquivalence(const VariablePtr &variable1, const VariablePtr &variable2)
{
    auto parent1 = variable1->parent();
    auto parent2 = variable2->parent();

    return isEntityChildOf(parent1, parent2)
           || isEntityChildOf(parent2, parent1)
           || areEntitiesSiblings(parent1, parent2);
}

bool interfaceTypeIsCompatible(Variable::InterfaceType interfaceTypeMinimumRequired, const std::string &interfaceTypeCompatibleWith)
{
    std::string interfaceTypeMinimumRequiredString = interfaceTypeToString.find(interfaceTypeMinimumRequired)->second;
    return interfaceTypeCompatibleWith.find(interfaceTypeMinimumRequiredString) != std::string::npos;
}

void Validator::ValidatorImpl::validateVariableInterface(const VariablePtr &variable, VariableMap &alreadyReported) const
{
    Variable::InterfaceType interfaceType = determineInterfaceType(variable);
    auto component = owningComponent(variable);
    std::string componentName = component->name();
    if (interfaceType == Variable::InterfaceType::NONE) {
        for (size_t index = 0; index < variable->equivalentVariableCount(); ++index) {
            const auto equivalentVariable = variable->equivalentVariable(index);
            auto equivalentComponent = owningComponent(equivalentVariable);
            if (equivalentComponent != nullptr && !reachableEquivalence(variable, equivalentVariable)) {
                auto it = std::find_if(alreadyReported.begin(), alreadyReported.end(),
                                       [equivalentVariable, variable](const VariablePairPtr &in) {
                                           return (in->variable1() == equivalentVariable) && (in->variable2() == variable);
                                       });
                if (it == alreadyReported.end()) {
                    VariablePairPtr pair = VariablePair::create(variable, equivalentVariable);
                    alreadyReported.push_back(pair);
                    std::string equivalentComponentName = equivalentComponent->name();

                    IssuePtr err = Issue::create();
                    err->setDescription("The equivalence between '" + variable->name() + "' in component '" + componentName + "'  and '" + equivalentVariable->name() + "' in component '" + equivalentComponentName + "' is invalid. Component '" + componentName + "' and '" + equivalentComponentName + "' are neither siblings nor in a parent/child relationship.");
                    err->setMapVariables(VariablePair::create(variable, equivalentVariable));
                    err->setReferenceRule(Issue::ReferenceRule::MAP_VARIABLES_AVAILABLE_INTERFACE);
                    mValidator->addIssue(err);
                }
            }
        }
    } else {
        auto interfaceTypeString = variable->interfaceType();
        if (!interfaceTypeIsCompatible(interfaceType, interfaceTypeString)) {
            IssuePtr err = Issue::create();
            if (interfaceTypeString.empty()) {
                err->setDescription("Variable '" + variable->name() + "' in component '" + componentName + "' has no interface type set. The interface type required is '" + interfaceTypeToString.find(interfaceType)->second + "'.");
            } else {
                err->setDescription("Variable '" + variable->name() + "' in component '" + componentName + "' has an interface type set to '" + interfaceTypeString + "' which is not the correct interface type for this variable. The interface type required is '" + interfaceTypeToString.find(interfaceType)->second + "'.");
            }
            err->setVariable(variable);
            err->setReferenceRule(Issue::ReferenceRule::MAP_VARIABLES_AVAILABLE_INTERFACE);
            mValidator->addIssue(err);
        }
    }
}

void Validator::ValidatorImpl::validateEquivalenceUnits(const ModelPtr &model, const VariablePtr &variable, VariableMap &alreadyReported) const
{
    std::string hints;
    ComponentPtr parentComponent = owningComponent(variable);

    if (variable->units() == nullptr) {
        // If the variable has no units, don't check it.
        return;
    }

    for (size_t index = 0; index < variable->equivalentVariableCount(); ++index) {
        auto equivalentVariable = variable->equivalentVariable(index);

        // If the parent component of the variable is nonexistent or imported, don't check it.
        auto equivalentComponent = owningComponent(equivalentVariable);
        if ((equivalentComponent == nullptr) || equivalentComponent->isImport()) {
            continue;
        }

        // If the equivalent variable has no units, don't check it.
        if (equivalentVariable->units() == nullptr) {
            continue;
        }

        double multiplier = 0.0;
        if (!unitsAreEquivalent(model, variable, equivalentVariable, hints, multiplier)) {
            auto it = std::find_if(alreadyReported.begin(), alreadyReported.end(),
                                   [equivalentVariable, variable](const VariablePairPtr &in) {
                                       return (in->variable1() == equivalentVariable) && (in->variable2() == variable);
                                   });
            if (it == alreadyReported.end()) {
                VariablePairPtr pair = VariablePair::create(variable, equivalentVariable);
                alreadyReported.push_back(pair);
                IssuePtr err = Issue::create();
                err->setDescription("Variable '" + variable->name() + "' in component '" + parentComponent->name() + "' has units of '" + variable->units()->name() + "' and an equivalent variable '" + equivalentVariable->name() + "' in component '" + equivalentComponent->name() + "' with non-matching units of '" + equivalentVariable->units()->name() + "'. The mismatch is: " + hints);
                err->setMapVariables(VariablePair::create(variable, equivalentVariable));
                err->setReferenceRule(Issue::ReferenceRule::MAP_VARIABLES_IDENTICAL_UNIT_REDUCTION);
                mValidator->addIssue(err);
            }
        }
    }
}

void Validator::ValidatorImpl::validateEquivalenceStructure(const VariablePtr &variable) const
{
    for (size_t index = 0; index < variable->equivalentVariableCount(); ++index) {
        auto equivalentVariable = variable->equivalentVariable(index);
        if (equivalentVariable->hasEquivalentVariable(variable)) {
            auto component = owningComponent(equivalentVariable);
            if (component == nullptr) {
                IssuePtr err = Issue::create();
                err->setDescription("Variable '" + equivalentVariable->name() + "' is an equivalent variable to '" + variable->name() + "' but '" + equivalentVariable->name() + "' has no parent component.");
                err->setMapVariables(VariablePair::create(variable, equivalentVariable));
                err->setReferenceRule(Issue::ReferenceRule::MAP_VARIABLES_VARIABLE1);
                mValidator->addIssue(err);
            }
        }
    }
}

void Validator::ValidatorImpl::validateConnections(const ModelPtr &model) const
{
    VariableMap interfaceErrorsAlreadyReported;
    VariableMap equivalentUnitErrorsAlreadyReported;

    VariablePtrs variables;

    for (size_t index = 0; index < model->componentCount(); ++index) {
        findAllVariablesWithEquivalences(model->component(index), variables);
    }

    for (const VariablePtr &variable : variables) {
        auto parentComponent = owningComponent(variable);
        if (parentComponent->isImport()) {
            continue;
        }
        validateVariableInterface(variable, interfaceErrorsAlreadyReported);
        validateEquivalenceUnits(model, variable, equivalentUnitErrorsAlreadyReported);
        validateEquivalenceStructure(variable);
    }
}

bool Validator::ValidatorImpl::isSupportedMathMLElement(const XmlNodePtr &node) const
{
    return (node->namespaceUri() == MATHML_NS)
           && std::find(supportedMathMLElements.begin(), supportedMathMLElements.end(), node->name()) != supportedMathMLElements.end();
}

Issue::ReferenceRule validateCellmlIdentifier(const std::string &name)
{
    // One or more alphabetic characters.
    if (!name.empty()) {
        // Does not start with numeric character.
        if (isdigit(name[0]) != 0) {
            return Issue::ReferenceRule::DATA_REPR_IDENTIFIER_BEGIN_EURO_NUM;
        }
        // Basic Latin alphanumeric characters and underscores.
        if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) {
            return Issue::ReferenceRule::DATA_REPR_IDENTIFIER_LATIN_ALPHANUM;
        }
    } else {
        // Empty string.
        return Issue::ReferenceRule::DATA_REPR_IDENTIFIER_AT_LEAST_ONE_ALPHANUM;
    }
    return Issue::ReferenceRule::UNDEFINED;
}

/**
 * @brief Test to determine if the @p name is a valid CellML identifier.
 *
 * Test to determine if the @p name is a valid CellML identifier.
 *
 * @param name The name to test.
 *
 * @return True if the name is a valid CellML identifier, false otherwise.
 */
bool isCellmlIdentifier(const std::string &name)
{
    Issue::ReferenceRule isValid = validateCellmlIdentifier(name);
    return isValid == Issue::ReferenceRule::UNDEFINED;
}

/**
 * @brief Test to determine if character is a valid xml name start char.
 *
 * Start name char is defined here: https://www.w3.org/TR/xml11/#NT-NameStartChar.
 *
 * @param startChar The character to test.
 *
 * @return True if the character is in the allowed unicode ranges for a start character in a name.
 */
bool isNameStartChar(uint32_t startChar)
{
    // ":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6] | [#xD8-#xF6] | [#xF8-#x2FF] | [#x370-#x37D] | [#x37F-#x1FFF] | [#x200C-#x200D] | [#x2070-#x218F] | [#x2C00-#x2FEF] | [#x3001-#xD7FF] | [#xF900-#xFDCF] | [#xFDF0-#xFFFD] | [#x10000-#xEFFFF]
    if ((startChar == 0x3Au) ||
            (0x41u <= startChar && startChar <= 0x5Au) ||
            (startChar == 0x5Fu) ||
            (0x61u <= startChar && startChar <= 0x7Au) ||
            (0xC0u <= startChar && startChar <= 0xD6u) ||
            (0xD8u <= startChar && startChar <= 0xF6u) ||
            (0xF8u <= startChar && startChar <= 0x2FFu) ||
            (0x370u <= startChar && startChar <= 0x37Du) ||
            (0x37Fu <= startChar && startChar <= 0x1FFFu) ||
            (0x200Cu <= startChar && startChar <= 0x200Du) ||
            (0x2070u <= startChar && startChar <= 0x218Fu) ||
            (0x2C00u <= startChar && startChar <= 0x2FEFu) ||
            (0x3001u <= startChar && startChar <= 0xD7FFu) ||
            (0xF900u <= startChar && startChar <= 0xFDCFu) ||
            (0xFDF0u <= startChar && startChar <= 0xFFFDu) ||
            (0x10000u <= startChar && startChar <= 0xEFFFFu)) {
        return true;
    }

    return false;
}

/**
 * @brief Breakdown a string into codepoints.
 *
 * Breakdown the @p text into a vector of codepoints as described by
 * @c uint32_t.
 *
 * @param text The std::string to breakdown.
 *
 * @return A vector of @c uint32_t of the unicode character values.
 */
std::vector<uint32_t> characterBreakdown(const std::string &text)
{
    std::vector<uint32_t> breakdown;
    std::vector<uint8_t> bitShifts = {24, 16, 8, 0};
    for (size_t i = 0; i < text.length();) {
        int codepointLength = 1;
        uint32_t value = 0;
        if ((text[i] & 0xf8) == 0xf0) {
            codepointLength = 4;
            auto subText = text.substr(i, 4);
            size_t index = 0;
            for (size_t j = 0; j < 4; ++j) {
                uint32_t tempValue = static_cast<uint8_t>(subText[index++]) << bitShifts[j];
                value |= tempValue;
            }
            breakdown.push_back(value);
        } else if ((text[i] & 0xf0) == 0xe0) {
            codepointLength = 3;
            auto subText = text.substr(i, 3);
            size_t index = 0;
            for (size_t j = 1; j < 4; ++j) {
                uint32_t tempValue = static_cast<uint8_t>(subText[index++]) << bitShifts[j];
                value |= tempValue;
            }
            breakdown.push_back(value);
        } else if ((text[i] & 0xe0) == 0xc0) {
            codepointLength = 2;
            auto subText = text.substr(i, 2);
            size_t index = 0;
            for (size_t j = 2; j < 4; ++j) {
                uint32_t tempValue = static_cast<uint8_t>(subText[index++]) << bitShifts[j];
                value |= tempValue;
            }
            breakdown.push_back(value);
        } else {
            auto subText = text.substr(i, 1);
            value = subText[0];
            breakdown.push_back(value);
        }

//        if ((i + codepointLength) > text.length()) {
//            codepointLength = 1;
//        }

        i += codepointLength;
    }

    return breakdown;
}

/**
 * @brief Test to determine if character is a valid xml name char.
 *
 * Name char is defined here: https://www.w3.org/TR/xml11/#NT-NameChar.
 *
 * @param nameChar The character to test.
 *
 * @return True if the character is in the allowed unicode ranges for a name.
 */
bool isNameChar(uint32_t nameChar)
{
    if (isNameStartChar(nameChar)) {
        return true;
    }
    // "-" | "." | [0-9] | #xB7 | [#x0300-#x036F] | [#x203F-#x2040]
    if ((0x30u <= nameChar && nameChar <= 0x39u) ||
            (nameChar == 0x2Du) ||
            (nameChar == 0x2Eu) ||
            (nameChar == 0xB7u) ||
            (0x0300u <= nameChar && nameChar <= 0x036Fu) ||
            (0x203Fu <= nameChar && nameChar <= 0x2040u)) {
        return true;
    }
    return false;
}

/**
 * @brief Test to determine if name is a valid xml name.
 *
 * Name is defined here: https://www.w3.org/TR/xml11/#NT-Name.
 *
 * @param name The @c std::string to test.
 *
 * @return True if the name is a valid xml name.
 */
bool isValidW3IdName(const std::string &name)
{
    if (!name.empty()) {
        auto breakdown = characterBreakdown(name);
        if (!isNameStartChar(breakdown[0])) {
            return false;
        }
        for (size_t i = 1; i < breakdown.size(); ++i) {
            if (!isNameChar(breakdown[i])) {
                return false;
            }
        }
    }
    return true;
}

IssuePtr makeIssueIllegalIdentifier(const std::string &name)
{
    IssuePtr issue = Issue::create();
    auto referenceRule = validateCellmlIdentifier(name);
    issue->setReferenceRule(referenceRule);

    if (referenceRule == Issue::ReferenceRule::DATA_REPR_IDENTIFIER_AT_LEAST_ONE_ALPHANUM) {
        // One or more alphabetic characters.
        issue->setDescription("CellML identifiers must contain one or more basic Latin alphabetic characters.");
    } else if (referenceRule == Issue::ReferenceRule::DATA_REPR_IDENTIFIER_BEGIN_EURO_NUM) {
        // Does not start with numeric character.
        issue->setDescription("CellML identifiers must not begin with a European numeric character [0-9].");
    } else if (referenceRule == Issue::ReferenceRule::DATA_REPR_IDENTIFIER_LATIN_ALPHANUM) {
        // Basic Latin alphanumeric characters and underscores.
        issue->setDescription("CellML identifiers must not contain any characters other than [a-zA-Z0-9_].");
    }

    return issue;
}

bool unitsAreEquivalent(const ModelPtr &model,
                        const VariablePtr &v1,
                        const VariablePtr &v2,
                        std::string &hints,
                        double &multiplier)
{
    std::map<std::string, double> unitMap = {};

    for (const auto &baseUnits : baseUnitsList) {
        unitMap.emplace(baseUnits, 0.0);
    }

    std::string ref;
    hints = "";
    multiplier = 0.0;

    if (model->hasUnits(v1->units()->name())) {
        UnitsPtr u1 = Units::create();
        u1 = model->units(v1->units()->name());
        updateBaseUnitCount(model, unitMap, multiplier, u1->name(), 1, 0, 1);
    } else if (unitMap.find(v1->units()->name()) != unitMap.end()) {
        ref = v1->units()->name();
        unitMap.at(ref) += 1.0;
    } else if (isStandardUnitName(v1->units()->name())) {
        updateBaseUnitCount(model, unitMap, multiplier, v1->units()->name(), 1, 0, 1);
    }

    if (model->hasUnits(v2->units()->name())) {
        UnitsPtr u2 = Units::create();
        u2 = model->units(v2->units()->name());
        updateBaseUnitCount(model, unitMap, multiplier, u2->name(), 1, 0, -1);
    } else if (unitMap.find(v2->units()->name()) != unitMap.end()) {
        ref = v2->units()->name();
        unitMap.at(v2->units()->name()) -= 1.0;
    } else if (isStandardUnitName(v2->units()->name())) {
        updateBaseUnitCount(model, unitMap, multiplier, v2->units()->name(), 1, 0, -1);
    }

    // Remove "dimensionless" from base unit testing.
    unitMap.erase("dimensionless");

    bool status = true;
    for (const auto &basePair : unitMap) {
        if (basePair.second != 0.0) {
            std::string num = std::to_string(basePair.second);
            num.erase(num.find_last_not_of('0') + 1, num.length());
            if (num.back() == '.') {
                num.pop_back();
            }
            hints += basePair.first + "^" + num + ", ";
            status = false;
        }
    }

    if (multiplier != 0.0) {
        // NB: multiplication issues are only reported when there is a base issue mismatch too, does not trigger it alone.
        // The multiplication mismatch will be returned through the multiplier argument in all cases.

        std::string num = std::to_string(multiplier);
        num.erase(num.find_last_not_of('0') + 1, num.length());
        if (num.back() == '.') {
            num.pop_back();
        }
        hints += "multiplication factor of 10^" + num + ", ";
    }

    // Remove the final trailing comma from the hints string.
    if (hints.length() > 2) {
        hints.pop_back();
        hints.back() = '.';
    }

    return status;
}

void updateBaseUnitCount(const ModelPtr &model,
                         std::map<std::string, double> &unitMap,
                         double &multiplier,
                         const std::string &uName,
                         double uExp, double logMult,
                         int direction)
{
    if (model->hasUnits(uName)) {
        UnitsPtr u = model->units(uName);
        if (!u->isBaseUnit()) {
            std::string ref;
            std::string pre;
            std::string id;
            double exp;
            double mult;
            double expMult;
            for (size_t i = 0; i < u->unitCount(); ++i) {
                u->unitAttributes(i, ref, pre, exp, expMult, id);
                mult = std::log10(expMult);
                if (!isStandardUnitName(ref)) {
                    updateBaseUnitCount(model, unitMap, multiplier, ref, exp * uExp, logMult + mult * uExp + standardPrefixList.at(pre) * uExp, direction);
                } else {
                    for (const auto &iter : standardUnitsList.at(ref)) {
                        unitMap.at(iter.first) += direction * (iter.second * exp * uExp);
                    }
                    multiplier += direction * (logMult + (standardMultiplierList.at(ref) + mult + standardPrefixList.at(pre)) * exp);
                }
            }
        } else if (unitMap.find(uName) == unitMap.end()) {
            unitMap.emplace(uName, direction * uExp);
            multiplier += direction * logMult;
        }
    } else if (isStandardUnitName(uName)) {
        for (const auto &iter : standardUnitsList.at(uName)) {
            unitMap.at(iter.first) += direction * (iter.second * uExp);
        }
        multiplier += direction * (logMult + standardMultiplierList.at(uName));
    }
}

void Validator::ValidatorImpl::checkUnitsForCycles(const ModelPtr &model, const UnitsPtr &parent,
                                                   std::vector<std::string> &history,
                                                   IssuesList &errorList) const
{
    if (parent->isBaseUnit()) {
        return;
    }

    // Recursive function to check for self-referencing in unit definitions.
    std::string id;
    std::string ref;
    std::string prefix;
    double exp;
    double mult;

    // Take history, and copy it for each new branch.
    for (size_t i = 0; i < parent->unitCount(); ++i) {
        parent->unitAttributes(i, ref, prefix, exp, mult, id);
        if (std::find(history.begin(), history.end(), ref) != history.end()) {
            history.push_back(ref);
            // Print to error output *only* when the first and last units are the same
            // otherwise we get lasso shapes reported.
            if (history.front() == history.back()) {
                errorList.push_back(history);
            }
        } else {
            // Step into dependencies if they are not built-in units.
            if (model->hasUnits(ref)) {
                UnitsPtr child = model->units(ref);
                history.push_back(ref);
                // Making a copy of the history vector to this point.
                std::vector<std::string> childHistory(history);
                checkUnitsForCycles(model, child, childHistory, errorList);
            }
        }
    }
}

void Validator::ValidatorImpl::checkUniqueIds(const ModelPtr &model)
{
    auto idMap = buildModelIdMap(model);

    for (const auto &id : idMap) {
        if (id.second.first > 1) {
            auto desc = "Duplicated identifier attribute '" + id.first + "' has been found in:\n";
            size_t i = 0;
            size_t iMax = id.second.second.size();
            for (const auto &item : id.second.second) {
                desc += item;
                ++i;
                if (i < iMax - 1) {
                    desc += ";\n";
                } else if (i == iMax - 1) {
                    desc += "; and\n";
                } else if (i == iMax) {
                    desc += ".\n";
                }
            }
            auto issue = libcellml::Issue::create();
            issue->setReferenceRule(Issue::ReferenceRule::DATA_REPR_IDENTIFIER_IDENTICAL);
            issue->setLevel(Issue::Level::ERROR);
            issue->setDescription(desc);
            issue->setModel(model);
            mValidator->addIssue(issue);
        }
    }
}

void Validator::ValidatorImpl::addIdMapItem(const std::string &id, const std::string &info, IdMap &idMap)
{
    if (idMap.count(id) > 0) {
        idMap[id].second.emplace_back(info);
        idMap[id] = std::make_pair(idMap[id].first + 1, idMap[id].second);
    } else {
        std::vector<std::string> infos;
        infos.emplace_back(info);
        idMap.emplace(id, std::make_pair(1, infos));
    }
}

IdMap Validator::ValidatorImpl::buildModelIdMap(const ModelPtr &model)
{
    IdMap idMap;
    std::string info;
    std::set<std::string> reportedConnections;
    // Model.
    if (!model->id().empty()) {
        info = " - model '" + model->name() + "'";
        addIdMapItem(model->id(), info, idMap);
    }

    // Units.
    for (size_t u = 0; u < model->unitsCount(); ++u) {
        auto units = model->units(u);
        if (!units->id().empty()) {
            if (units->isImport()) {
                info = " - imported units '" + units->name() + "' in model '" + model->name() + "'";
            } else {
                info = " - units '" + units->name() + "' in model '" + model->name() + "'";
            }
            addIdMapItem(units->id(), info, idMap);
        }
        for (size_t i = 0; i < units->unitCount(); ++i) {
            std::string reference;
            std::string prefix;
            double exponent;
            double multiplier;
            std::string id;
            units->unitAttributes(i, reference, prefix, exponent, multiplier, id);
            if (!id.empty()) {
                info = " - unit in units '" + units->name() + "' in model '" + model->name() + "'";
                addIdMapItem(id, info, idMap);
            }
        }
        if (units->isImport() && units->importSource() != nullptr && !units->importSource()->id().empty()) {
            info = " - import source for units '" + units->name() + "'";
            addIdMapItem(units->importSource()->id(), info, idMap);
        }
    }
    // Encapsulation.
    if (!model->encapsulationId().empty()) {

        // Check for a valid identifier.
        if (!isValidW3IdName(model->encapsulationId())) {
            IssuePtr issue = Issue::create();
            issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
            issue->setModel(model);
            issue->setDescription("Model '" + model->name() + "' does not have a valid encapsulation 'id' attribute, '" + model->encapsulationId() + "'.");
            mValidator->addIssue(issue);
        }

        info = " - encapsulation in model '" + model->name() + "'";
        addIdMapItem(model->encapsulationId(), info, idMap);
    }

    // Start recursion through encapsulation hierarchy.
    for (size_t c = 0; c < model->componentCount(); ++c) {
        buildComponentIdMap(model->component(c), idMap, reportedConnections);
    }
    return idMap;
}

void Validator::ValidatorImpl::buildComponentIdMap(const ComponentPtr &component, IdMap &idMap, std::set<std::string> &reportedConnections)
{
    std::string info;

    // Component.
    if (!component->id().empty()) {
        std::string imported;
        std::string owning;
        if (component->isImport()) {
            imported = "imported ";
        }
        if (owningComponent(component) != nullptr) {
            owning = "' in component '" + owningComponent(component)->name() + "'";
        } else {
            owning = "' in model '" + owningModel(component)->name() + "'";
        }
        info = " - " + imported + "component '" + component->name() + owning;
        addIdMapItem(component->id(), info, idMap);
    }

    // Variables.
    for (size_t i = 0; i < component->variableCount(); ++i) {
        auto item = component->variable(i);
        if (!item->id().empty()) {
            info = " - variable '" + item->name() + "' in component '" + component->name() + "'";
            addIdMapItem(item->id(), info, idMap);
        }
        // Equivalent variables.
        for (size_t e = 0; e < item->equivalentVariableCount(); ++e) {
            auto equiv = item->equivalentVariable(e);
            auto equivParent = owningComponent(equiv);
            if (equivParent != nullptr) {
                // Skipping half of the equivalences to avoid duplicate reporting.
                std::string s1 = item->name() + component->name();
                std::string s2 = equiv->name() + equivParent->name();
                std::string mappingId = Variable::equivalenceMappingId(item, equiv);
                // Variable mapping.
                if ((s1 < s2) && !mappingId.empty()) {
                    std::string mappingDescription =
                            "between variable '" + item->name() + "' in component '" + component->name()
                            + "' and variable '" + equiv->name() + "' in component '" + equivParent->name() + "'";
                    // Check for a valid identifier.
                    if (!isValidW3IdName(mappingId)) {
                        IssuePtr issue = Issue::create();
                        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
                        issue->setMapVariables(item, equiv);
                        issue->setDescription("Variable equivalence " + mappingDescription + ", does not have a valid map_variables 'id' attribute, '" + mappingId + "'.");
                        mValidator->addIssue(issue);
                    }

                    info = " - variable equivalence " + mappingDescription;
                    addIdMapItem(mappingId, info, idMap);
                }
                // Connections.
                auto connectionId = Variable::equivalenceConnectionId(item, equiv);
                std::string connection = component->name() < equivParent->name() ? component->name() + equivParent->name() : equivParent->name() + component->name();
                if ((s1 < s2) && !connectionId.empty() && (reportedConnections.count(connection) == 0)) {
                    std::string connectionDescription =
                            "between components '" + component->name() + "' and '" + equivParent->name()
                            + "' because of variable equivalence between variables '" + item->name()
                            + "' and '" + equiv->name() + "'";
                    // Check for a valid identifier.
                    if (!isValidW3IdName(connectionId)) {
                        IssuePtr issue = Issue::create();
                        issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
                        issue->setConnection(item, equiv);
                        issue->setDescription("Connection " + connectionDescription + ", does not have a valid connection 'id' attribute, '" + connectionId + "'.");
                        mValidator->addIssue(issue);
                    }

                    reportedConnections.insert(connection);
                    info = " - connection " + connectionDescription;
                    addIdMapItem(connectionId, info, idMap);
                }
            }
        }
    }

    // Resets.
    for (size_t i = 0; i < component->resetCount(); ++i) {
        auto item = component->reset(i);
        if (!item->id().empty()) {
            info = " - reset at index " + std::to_string(i) + " in component '" + component->name() + "'";
            addIdMapItem(item->id(), info, idMap);
        }
        if (!item->testValueId().empty()) {
            info = " - test_value in reset at index " + std::to_string(i) + " in component '" + component->name() + "'";
            addIdMapItem(item->testValueId(), info, idMap);
        }
        info = "test_value in reset " + std::to_string(i) + " in component '" + component->name() + "'";
        buildMathIdMap(info, idMap, item->testValue());
        if (!item->resetValueId().empty()) {
            info = " - reset_value in reset at index " + std::to_string(i) + " in component '" + component->name() + "'";
            addIdMapItem(item->resetValueId(), info, idMap);
        }
        info = "reset_value in reset " + std::to_string(i) + " in component '" + component->name() + "'";
        buildMathIdMap(info, idMap, item->resetValue());
    }

    // Maths.
    info = "math in component '" + component->name() + "'";
    buildMathIdMap(info, idMap, component->math());

    // Imports.
    if (component->isImport() && (component->importSource() != nullptr) && !component->importSource()->id().empty()) {
        info = " - import source for component '" + component->name() + "'";
        addIdMapItem(component->importSource()->id(), info, idMap);
    }

    // Hierarchy.
    if (!component->encapsulationId().empty()) {

        // Check for a valid identifier.
        if (!isValidW3IdName(component->encapsulationId())) {
            IssuePtr issue = Issue::create();
            issue->setReferenceRule(Issue::ReferenceRule::XML_ID_ATTRIBUTE);
            issue->setComponent(component);
            issue->setDescription("Component '" + component->name() + "' does not have a valid encapsulation 'id' attribute, '" + component->encapsulationId() + "'.");
            mValidator->addIssue(issue);
        }

        info = " - encapsulation component_ref to component '" + component->name() + "'";
        addIdMapItem(component->encapsulationId(), info, idMap);
    }

    // Child components.
    for (size_t c = 0; c < component->componentCount(); ++c) {
        buildComponentIdMap(component->component(c), idMap, reportedConnections);
    }
}

void Validator::ValidatorImpl::buildMathIdMap(const std::string &infoRef, IdMap &idMap, const std::string &input)
{
    std::vector<XmlDocPtr> docs = multiRootXml(input);

    for (const auto &doc : docs) {
        XmlNodePtr node = doc->rootNode();
        if (node == nullptr) {
            return;
        }
        if (!node->isMathmlElement("math")) {
            continue;
        }
        buildMathChildIdMap(node, infoRef, idMap);
    }
}

void Validator::ValidatorImpl::buildMathChildIdMap(const XmlNodePtr &node, const std::string &infoRef, IdMap &idMap)
{
    std::string info;
    XmlAttributePtr attribute = node->firstAttribute();
    while (attribute != nullptr) {
        if (attribute->isType("id")) {
            std::string variable;
            if (node->name() == "ci") {
                if (node->firstChild() != nullptr) {
                    variable = "'" + node->firstChild()->convertToString() + "' ";
                }
            }
            info = " - MathML " + node->name() + " element " + variable + "in " + infoRef;
            addIdMapItem(attribute->value(), info, idMap);
        }
        attribute = attribute->next();
    }
    XmlNodePtr childNode = node->firstChild();
    while (childNode != nullptr) {
        buildMathChildIdMap(childNode, infoRef, idMap);
        childNode = childNode->next();
    }
}

} // namespace libcellml
