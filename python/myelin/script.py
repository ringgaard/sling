# Copyright 2021 Ringgaard Research ApS
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Myelin script compiler."""

import dis
import opcode

import sling.pysling as api

from .flow import Flow
from .builder import Builder

verbose = False

# Type class for representing tensor types.
class Type(object):
  def __init__(self, dt, dims=None):
    self.dt = dt;
    self.dims = dims

  def __getitem__(self, dims):
    if type(dims) is tuple:
      return Type(self.dt, list(dims))
    else:
      return Type(self.dt, [dims])

  def __repr__(self):
    if self.dims is None:
      return self.dt
    else:
      return self.dt + str(self.dims)

# Basic tensor types.
float32 = Type("float32")
float64 = Type("float64")

int8 = Type("int8")
int16 = Type("int16")
int32 = Type("int32")
int64 = Type("int64")

# Python opcodes.
LOAD_GLOBAL = opcode.opmap["LOAD_GLOBAL"]
LOAD_FAST = opcode.opmap["LOAD_FAST"]
LOAD_CONST = opcode.opmap["LOAD_CONST"]
STORE_FAST = opcode.opmap["STORE_FAST"]
CALL_FUNCTION = opcode.opmap["CALL_FUNCTION"]
RETURN_VALUE = opcode.opmap["RETURN_VALUE"]

BINARY_ADD = opcode.opmap["BINARY_ADD"]
BINARY_SUBTRACT  = opcode.opmap["BINARY_SUBTRACT"]
BINARY_MULTIPLY = opcode.opmap["BINARY_MULTIPLY"]
BINARY_MODULO = opcode.opmap["BINARY_MODULO"]
BINARY_MATRIX_MULTIPLY = opcode.opmap["BINARY_MATRIX_MULTIPLY"]
BINARY_POWER  = opcode.opmap["BINARY_POWER"]
BINARY_TRUE_DIVIDE = opcode.opmap["BINARY_TRUE_DIVIDE"]
BINARY_AND = opcode.opmap["BINARY_AND"]
BINARY_OR = opcode.opmap["BINARY_OR"]
BINARY_XOR = opcode.opmap["BINARY_XOR"]
COMPARE_OP = opcode.opmap["COMPARE_OP"]
UNARY_NEGATIVE = opcode.opmap["UNARY_NEGATIVE"]
UNARY_NOT = opcode.opmap["UNARY_NOT"]
INPLACE_ADD = opcode.opmap["INPLACE_ADD"]
INPLACE_SUBTRACT = opcode.opmap["INPLACE_SUBTRACT"]
INPLACE_MULTIPLY = opcode.opmap["INPLACE_MULTIPLY"]
INPLACE_MATRIX_MULTIPLY = opcode.opmap["INPLACE_MATRIX_MULTIPLY"]
INPLACE_TRUE_DIVIDE = opcode.opmap["INPLACE_TRUE_DIVIDE"]
INPLACE_MODULO = opcode.opmap["INPLACE_MODULO"]
INPLACE_POWER = opcode.opmap["INPLACE_POWER"]
INPLACE_AND = opcode.opmap["INPLACE_AND"]
INPLACE_XOR = opcode.opmap["INPLACE_XOR"]
INPLACE_OR = opcode.opmap["INPLACE_OR"]

# Myelin op emitters.
opemitters = {
  "concat": lambda b, args: b.conat(args),

  "add": lambda b, args: b.add(args[0], args[1]),
  "sub": lambda b, args: b.sub(args[0], args[1]),
  "mul": lambda b, args: b.mul(args[0], args[1]),
  "div": lambda b, args: b.div(args[0], args[1]),
  "mod": lambda b, args: b.mod(args[0], args[1]),
  "matmul": lambda b, args: b.matmul(args[0], args[1]),
  "t": lambda b, args: b.t(args[0], args[1]),
  "transpose": lambda b, args: b.t(args[0], args[1]),

  "minimum": lambda b, args: b.minimum(args[0], args[1]),
  "maximum": lambda b, args: b.maximum(args[0], args[1]),
  "argmin": lambda b, args: b.argmin(args[0]),
  "argmax": lambda b, args: b.argmax(args[0]),

  "pow": lambda b, args: b.exp(args[0], args[1]),
  "log": lambda b, args: b.log(args[0]),
  "exp": lambda b, args: b.exp(args[0]),
  "erf": lambda b, args: b.erf(args[0]),
  "sigmoid": lambda b, args: b.sigmoid(args[0]),
  "relu": lambda b, args: b.relu(args[0]),

  "sin": lambda b, args: b.sin(args[0]),
  "cos": lambda b, args: b.cos(args[0]),
  "tan": lambda b, args: b.tan(args[0]),
  "cot": lambda b, args: b.cot(args[0]),
  "sec": lambda b, args: b.sec(args[0]),
  "csc": lambda b, args: b.csc(args[0]),

  "asin": lambda b, args: b.asin(args[0]),
  "acos": lambda b, args: b.acos(args[0]),
  "atan": lambda b, args: b.atan(args[0]),
  "acot": lambda b, args: b.acot(args[0]),
  "asec": lambda b, args: b.asec(args[0]),
  "acsc": lambda b, args: b.acsc(args[0]),

  "sinh": lambda b, args: b.sinh(args[0]),
  "cosh": lambda b, args: b.cosh(args[0]),
  "tanh": lambda b, args: b.tanh(args[0]),
  "coth": lambda b, args: b.coth(args[0]),
  "sech": lambda b, args: b.sech(args[0]),
  "csch": lambda b, args: b.csch(args[0]),

  "asinh": lambda b, args: b.asinh(args[0]),
  "acosh": lambda b, args: b.acosh(args[0]),
  "atanh": lambda b, args: b.atanh(args[0]),
  "acoth": lambda b, args: b.acoth(args[0]),
  "asech": lambda b, args: b.asech(args[0]),
  "acsch": lambda b, args: b.acsch(args[0]),

  "square": lambda b, args: b.square(args[0]),
  "sqrt": lambda b, args: b.sqrt(args[0]),
  "rsqrt": lambda b, args: b.rsqrt(args[0]),
  "neg": lambda b, args: b.neg(args[0]),
  "abs": lambda b, args: b.abs(args[0]),
  "sign": lambda b, args: b.sign(args[0]),
  "rcp": lambda b, args: b.rcp(args[0]),
  "floor": lambda b, args: b.floor(args[0]),
  "ceil": lambda b, args: b.ceil(args[0]),
  "round": lambda b, args: b.round(args[0]),
  "trunc": lambda b, args: b.trunc(args[0]),

  "not": lambda b, args: b.logical_not(args[0]),
  "cond": lambda b, args: b.cond(args[0], args[1], args[2]),
  "select": lambda b, args: b.select(args[0], args[1]),
  "identity": lambda b, args: b.identity(args[0]),

  "sum": lambda b, args: b.sum(args[0]),
  "product": lambda b, args: b.product(args[0]),
  "min": lambda b, args: b.min(args[0]),
  "max": lambda b, args: b.max(args[0]),
  "all": lambda b, args: b.all(args[0]),
  "any": lambda b, args: b.any(args[0]),
  "count": lambda b, args: b.count(args[0]),

  "mean": lambda b, args: b.mean(args[0]),
  "norm": lambda b, args: b.norm(args[0]),
  "normalize": lambda b, args: b.normalize(args[0]),
  "softmax": lambda b, args: b.softmax(args[0]),
  "logsumexp": lambda b, args: b.logsumexp(args[0]),

  # Unsupported:
  # ref
  # shape
  # size
  # rank
  # assign
  # split
  # reshape
  # argm
  # gather
  # pooling_gather
  # gather_sum
  # gather_max
  # gather_avg
  # scatter
  # assign_add_scatter
  # onehot
}

# A script object contains the runtime information for a function.
class Script(object):
  def __init__(self, module, func):
    self.module = module
    self.func = func
    self.argcount = 0;
    self.cell = None
    self.argvars = []
    self.argidxs = []
    self.retvar = None
    self.retidx = None
    module.scripts.append(self)


  def link(self, net):
    self.cell = net.cell(self.func.name)
    self.argcount = len(self.argvars)
    for arg in self.argvars:
      idx = None
      if arg in self.cell: idx = self.cell.index(arg)
      self.argidxs.append(idx)
    if self.retvar in self.cell:
      self.retidx = self.cell.index(self.retvar)

# Module for defining and compiling script functions.
class Module(object):
  def __init__(self):
    self.flow = Flow()
    self.net = None
    self.scripts = []

  def build(self, func):
    # Set up builder for function.
    b = Builder(self.flow, func.__name__)

    # Create script object for function.
    script = Script(self, b.func)
    if verbose:
      dis.show_code(func)
      dis.dis(func)

    # Add function arguments as variables.
    variables = {}
    code = func.__code__
    for i in range(code.co_argcount):
      argname = code.co_varnames[i]
      argtype = func.__annotations__[argname]
      var = b.var(argname, argtype.dt, argtype.dims)
      var.input = True
      # TODO pass arguments by reference when input tensor sharing has been
      # disabled.
      #var.ref = True
      var.pyname = argname
      variables[argname] = var
      script.argvars.append(var)

    # Generate Myelin ops from Python byte code.
    stack = []
    for instr in dis.get_instructions(func):
      if instr.opcode == LOAD_GLOBAL:
        value = opemitters.get(instr.argval, instr.argval)
        stack.append(value)
      elif instr.opcode == LOAD_FAST:
        var = variables[instr.argval]
        stack.append(var)
      elif instr.opcode == LOAD_CONST:
        value = code.co_consts[instr.arg]
        stack.append(value)
      elif instr.opcode == STORE_FAST:
        value = stack.pop()
        varname = instr.argval
        value.pyname = varname
        variables[varname] = value
      elif instr.opcode == CALL_FUNCTION:
        numargs = instr.arg
        args = stack[-numargs:]
        stack = stack[0:-numargs]
        op = stack.pop()
        if type(op) is str: raise Exception("Unknown myelin op: " + op)
        result = op(b, args)
        stack.append(result)
      elif instr.opcode == RETURN_VALUE:
        result = stack.pop()
        result.output = True
        script.retvar = result
      elif instr.opcode == BINARY_ADD:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.add(x, y))
      elif instr.opcode == BINARY_SUBTRACT:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.sub(x, y))
      elif instr.opcode == BINARY_MULTIPLY:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.mul(x, y))
      elif instr.opcode == BINARY_MODULO:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.mod(x, y))
      elif instr.opcode == BINARY_MATRIX_MULTIPLY:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.matmul(x, y))
      elif instr.opcode == BINARY_TRUE_DIVIDE:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.div(x, y))
      elif instr.opcode == BINARY_AND:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.logical_and(x, y))
      elif instr.opcode == BINARY_OR:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.logical_or(x, y))
      elif instr.opcode == BINARY_XOR:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.logical_xor(x, y))
      elif instr.opcode == BINARY_POWER:
        y = stack.pop()
        x = stack.pop()
        stack.append(b.pow(x, y))
      elif instr.opcode == COMPARE_OP:
        op = instr.argval
        y = stack.pop()
        x = stack.pop()
        if op == "<":
          result = b.less(x, y)
        elif op == "<=":
          result = b.less_equal(x, y)
        elif op == "==":
          result = b.equal(x, y)
        elif op == "!=":
          result = b.not_equal(x, y)
        elif op == ">":
          result = b.greater(x, y)
        elif op == ">=":
          result = b.greater_equal(x, y)
        else:
          raise Exception("Unsupported comparison: " + op)
        stack.append(result)
      elif instr.opcode == UNARY_NEGATIVE:
        x = stack.pop()
        stack.append(b.neg(x, y))
      elif instr.opcode == UNARY_NOT:
        x = stack.pop()
        stack.append(b.logical_not(x, y))
      elif instr.opcode == INPLACE_ADD:
        val = stack.pop()
        var = stack.pop()
        result = b.add(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_SUBTRACT:
        val = stack.pop()
        var = stack.pop()
        result = b.sub(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_MULTIPLY:
        val = stack.pop()
        var = stack.pop()
        result = b.mul(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_MATRIX_MULTIPLY:
        val = stack.pop()
        var = stack.pop()
        result = b.matmul(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_TRUE_DIVIDE:
        val = stack.pop()
        var = stack.pop()
        result = b.div(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_MODULO:
        val = stack.pop()
        var = stack.pop()
        result = b.mod(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_POWER:
        val = stack.pop()
        var = stack.pop()
        result = b.pow(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_AND:
        val = stack.pop()
        var = stack.pop()
        result = b.logical_and(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_OR:
        val = stack.pop()
        var = stack.pop()
        result = b.logical_or(var, val)
        variables[var.pyname] = result
        stack.append(result)
      elif instr.opcode == INPLACE_XOR:
        val = stack.pop()
        var = stack.pop()
        result = b.logical_xor(var, val)
        variables[var.pyname] = result
        stack.append(result)
      else:
        raise Exception("Unsupported instruction: " + str(instr))

    return script

  # Decorator for turning Python function into a compiled Myelin function
  def function(self, func):
    # Build script from Python function
    script = self.build(func)

    # Wrapper function for running the compiled cell.
    def wrapper(*args):
      # Compile script on-demand.
      if script.cell is None: script.module.compile()

      # Create cell instance.
      data = script.cell.instance()

      # Set up arguments.
      for i in range(script.argcount):
        idx = script.argidxs[i]
        if idx is None: continue
        data[idx] = args[i]

      # Execute cell computation.
      data.compute()

      # Return result.
      if script.retidx is not None: return data[script.retidx]

    return wrapper

  # Compile and link module.
  def compile(self):
    # Compile flow.
    compiler = api.Compiler()
    self.net = compiler.compile(self.flow)

    # Link scripts to compiled network.
    for script in self.scripts:
      script.link(self.net)

