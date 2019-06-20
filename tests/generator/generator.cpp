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

#include "test_utils.h"

#include "gtest/gtest.h"

#include <libcellml>

static const std::string EMPTY_STRING;

TEST(Generator, emptyModel)
{
    libcellml::ModelPtr model = std::make_shared<libcellml::Model>();

    model->setName("emptyModel");

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(size_t(0), generator.errorCount());

    EXPECT_EQ(size_t(0), generator.stateCount());
    EXPECT_EQ(size_t(0), generator.rateCount());
    EXPECT_EQ(size_t(0), generator.variableCount());

    EXPECT_EQ(EMPTY_STRING, generator.neededMathMethods());
    EXPECT_EQ(EMPTY_STRING, generator.initializeVariables());
    EXPECT_EQ(EMPTY_STRING, generator.computeConstantEquations());
    EXPECT_EQ(EMPTY_STRING, generator.computeRateEquations());
    EXPECT_EQ(EMPTY_STRING, generator.computeAlgebraicEquations());
}

/*TODO: reenable this test once we can correctly type a model's variables.
TEST(Generator, coverage) {
//TODO: code should be generated for the coverage CellML file with and without
//      the Generator's private mHasXXX booleans set, so that we really cover
//      everything indeed.
    libcellml::Parser parser;
    libcellml::ModelPtr model = parser.parseModel(fileContents("generator/resources/coverage.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(size_t(0), generator.errorCount());
}
*/

TEST(Generator, initialized_variable_of_integration)
{
    libcellml::Parser parser;
    libcellml::ModelPtr model = parser.parseModel(fileContents("generator/resources/initialized_variable_of_integration.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    const std::vector<std::string> expectedErrors = {
        "Variable 'time' in component 'main' of model 'initialized_variable_of_integration' cannot be both a variable of integration and initialised."};

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(expectedErrors.size(), generator.errorCount());

    for (size_t i = 0; i < generator.errorCount(); ++i) {
        EXPECT_EQ(expectedErrors.at(i), generator.error(i)->description());
        EXPECT_EQ(libcellml::Error::Kind::GENERATOR, generator.error(i)->kind());
    }
}

TEST(Generator, two_variables_of_integration)
{
    libcellml::Parser parser;
    libcellml::ModelPtr model = parser.parseModel(fileContents("generator/resources/two_variables_of_integration.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    const std::vector<std::string> expectedErrors = {
        "Variable 'time' in component 'main' of model 'two_variables_of_integration' and variable 'other_time' in component 'sub_sub_sub' of model 'two_variables_of_integration' cannot both be a variable of integration."};

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(expectedErrors.size(), generator.errorCount());

    for (size_t i = 0; i < generator.errorCount(); ++i) {
        EXPECT_EQ(expectedErrors.at(i), generator.error(i)->description());
        EXPECT_EQ(libcellml::Error::Kind::GENERATOR, generator.error(i)->kind());
    }
}

TEST(Generator, non_first_order_odes)
{
    libcellml::Parser parser;
    libcellml::ModelPtr model = parser.parseModel(fileContents("generator/resources/non_first_order_odes.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    const std::vector<std::string> expectedErrors = {
        "The differential equation for variable 'x' in component 'main' of model 'non_first_order_odes' must be of the first order.",
        "The differential equation for variable 'y' in component 'sub' of model 'non_first_order_odes' must be of the first order.",
        "The differential equation for variable 'z' in component 'sub_sub' of model 'non_first_order_odes' must be of the first order."};

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(expectedErrors.size(), generator.errorCount());

    for (size_t i = 0; i < generator.errorCount(); ++i) {
        EXPECT_EQ(expectedErrors.at(i), generator.error(i)->description());
        EXPECT_EQ(libcellml::Error::Kind::GENERATOR, generator.error(i)->kind());
    }
}

TEST(Generator, variable_initialized_twice)
{
    libcellml::Parser parser;
    libcellml::ModelPtr model = parser.parseModel(fileContents("generator/resources/variable_initialized_twice.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    const std::vector<std::string> expectedErrors = {
        "Variable 'x' in component 'sub' of model 'variable_initialized_twice' and variable 'x' in component 'main' of model 'variable_initialized_twice' are equivalent and cannot therefore both be initialised."};

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(expectedErrors.size(), generator.errorCount());

    for (size_t i = 0; i < generator.errorCount(); ++i) {
        EXPECT_EQ(expectedErrors.at(i), generator.error(i)->description());
    }
}

TEST(Generator, non_initialized_state)
{
    libcellml::Parser parser;
    libcellml::ModelPtr model = parser.parseModel(fileContents("generator/resources/non_initialized_state.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    const std::vector<std::string> expectedErrors = {
        "Variable 'x' in component 'main' of model 'non_initialized_state' is used in an ODE, but it is not initialised."};

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(expectedErrors.size(), generator.errorCount());

    for (size_t i = 0; i < generator.errorCount(); ++i) {
        EXPECT_EQ(expectedErrors.at(i), generator.error(i)->description());
    }
}

/*TODO: reenable this test once we are done with the previous tests.
TEST(Generator, algebraic_eqn_derivative_on_rhs_one_component) {
    libcellml::Parser parser;
    libcellml::ModelPtr model = parser.parseModel(fileContents("generator/resources/algebraic_eqn_derivative_on_rhs_one_component/model.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(size_t(0), generator.errorCount());

    EXPECT_EQ(libcellml::Generator::Type::ALGEBRAIC, generator.type());

    EXPECT_EQ(size_t(1), generator.stateCount());
    EXPECT_EQ(size_t(2), generator.variableCount());

    EXPECT_EQ(EMPTY_STRING, generator.neededMathMethods());
    EXPECT_EQ(fileContents("generator/resources/algebraic_eqn_derivative_on_rhs_one_component/initializeVariables.out"),
              generator.initializeVariables());
    EXPECT_EQ(fileContents("generator/resources/algebraic_eqn_derivative_on_rhs_one_component/computeConstantEquations.out"),
              generator.computeConstantEquations());
    EXPECT_EQ(fileContents("generator/resources/algebraic_eqn_derivative_on_rhs_one_component/computeRateEquations.out"),
              generator.computeRateEquations());
    EXPECT_EQ(EMPTY_STRING,
              generator.computeAlgebraicEquations());

    generator.setWithNames(true);

    EXPECT_EQ(EMPTY_STRING, generator.neededMathMethods());
    EXPECT_EQ(fileContents("generator/resources/algebraic_eqn_derivative_on_rhs_one_component/initializeVariables_with_names.out"),
              generator.initializeVariables());
    EXPECT_EQ(fileContents("generator/resources/algebraic_eqn_derivative_on_rhs_one_component/computeConstantEquations_with_names.out"),
              generator.computeConstantEquations());
    EXPECT_EQ(fileContents("generator/resources/algebraic_eqn_derivative_on_rhs_one_component/computeRateEquations_with_names.out"),
              generator.computeRateEquations());
    EXPECT_EQ(EMPTY_STRING,
              generator.computeAlgebraicEquations());
}
*/

TEST(Generator, van_der_pol_model_1928)
{
    //TODO: code should be generated for the coverage CellML file with and without
    //      the Generator's private mHasXXX booleans set, so that we really cover
    //      everything indeed.
    libcellml::Parser parser;
    libcellml::ModelPtr model = parser.parseModel(fileContents("generator/resources/van_der_pol_model_1928/model.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(size_t(0), generator.errorCount());

    EXPECT_EQ(libcellml::Generator::Type::ODE, generator.type());

    EXPECT_EQ(size_t(2), generator.stateCount());
    EXPECT_EQ(size_t(1), generator.variableCount());
}

/*TODO: remove the below test once we are done testing things.
TEST(Generator, test) {
    libcellml::Parser parser;
libcellml::ModelPtr model = parser.parseModel(fileContents("../../../../Desktop/hodgkin_huxley_squid_axon_model_1952.cellml"));
//libcellml::ModelPtr model = parser.parseModel(fileContents("../../../../Desktop/noble_model_1962.cellml"));
//libcellml::ModelPtr model = parser.parseModel(fileContents("../../../../Desktop/zhang_SAN_model_2000_all.cellml"));

    EXPECT_EQ(size_t(0), parser.errorCount());

    libcellml::Generator generator;

    generator.processModel(model);

    EXPECT_EQ(size_t(0), generator.errorCount());
}
*/
