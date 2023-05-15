# The content of this file was generated using the Python profile of libCellML 0.4.0.

from enum import Enum
from math import *


__version__ = "0.4.0"
LIBCELLML_VERSION = "0.4.0"

STATE_COUNT = 1
VARIABLE_COUNT = 2


class VariableType(Enum):
    VARIABLE_OF_INTEGRATION = 0
    STATE = 1
    CONSTANT = 2
    COMPUTED_CONSTANT = 3
    ALGEBRAIC = 4


VOI_INFO = {"name": "t", "units": "second", "component": "environment", "type": VariableType.VARIABLE_OF_INTEGRATION}

STATE_INFO = [
    {"name": "v", "units": "dimensionless", "component": "my_ode", "type": VariableType.STATE}
]

VARIABLE_INFO = [
    {"name": "a", "units": "per_s", "component": "my_ode", "type": VariableType.COMPUTED_CONSTANT},
    {"name": "x", "units": "per_s", "component": "my_algebraic_eqn", "type": VariableType.ALGEBRAIC}
]


def create_states_array():
    return [0.0]*STATE_COUNT


def create_variables_array():
    return [0.0]*VARIABLE_COUNT


def initialise_variables(states, variables):
    variables[0] = 1.0
    states[0] = 1.0


def compute_computed_constants(variables):
    pass


def compute_rates(voi, states, rates, variables):
    rates[0] = variables[0]


def compute_variables(voi, states, rates, variables):
    variables[1] = rates[0]
