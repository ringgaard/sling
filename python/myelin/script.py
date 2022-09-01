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

import sys
import dis
import opcode

import sling.pysling as api
import sling.flags as flags

from .flow import Flow
from .builder import Builder

flags.define("--dump_bytecode",
             help="output Python bytecode for Myelin scripts",
             default=False,
             action="store_true")

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
BUILD_TUPLE = opcode.opmap["BUILD_TUPLE"]
UNPACK_SEQUENCE = opcode.opmap["UNPACK_SEQUENCE"]
COMPARE_OP = opcode.opmap["COMPARE_OP"]

# Myelin op emitters.

function_emitters = {
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

  # Not yet supported:
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

binary_emitters = {
  opcode.opmap["BINARY_ADD"]: lambda b, x, y: b.add(x, y),
  opcode.opmap["BINARY_SUBTRACT"]: lambda b, x, y: b.sub(x, y),
  opcode.opmap["BINARY_MULTIPLY"]: lambda b, x, y: b.mul(x, y),
  opcode.opmap["BINARY_MODULO"]: lambda b, x, y: b.mod(x, y),
  opcode.opmap["BINARY_MATRIX_MULTIPLY"]: lambda b, x, y: b.matmul(x, y),
  opcode.opmap["BINARY_TRUE_DIVIDE"]: lambda b, x, y: b.div(x, y),
  opcode.opmap["BINARY_AND"]: lambda b, x, y: b.logical_and(x, y),
  opcode.opmap["BINARY_OR"]: lambda b, x, y: b.logical_or(x, y),
  opcode.opmap["BINARY_XOR"]: lambda b, x, y: b.logical_xor(x, y),
  opcode.opmap["BINARY_POWER"]: lambda b, x, y: b.pow(x, y),
}

unary_emitters = {
  opcode.opmap["UNARY_NEGATIVE"]: lambda b, x: b.neg(x),
  opcode.opmap["UNARY_NOT"]: lambda b, x: b.logical_not(x),
}

inplace_emitters = {
  opcode.opmap["INPLACE_ADD"]: lambda b, x, y: b.add(x, y),
  opcode.opmap["INPLACE_SUBTRACT"]: lambda b, x, y: b.sub(x, y),
  opcode.opmap["INPLACE_MULTIPLY"]: lambda b, x, y: b.mul(x, y),
  opcode.opmap["INPLACE_MATRIX_MULTIPLY"]: lambda b, x, y: b.matmul(x, y),
  opcode.opmap["INPLACE_TRUE_DIVIDE"]: lambda b, x, y: b.div(x, y),
  opcode.opmap["INPLACE_MODULO"]: lambda b, x, y: b.mod(x, y),
  opcode.opmap["INPLACE_POWER"]: lambda b, x, y: b.pow(x, y),
  opcode.opmap["INPLACE_AND"]: lambda b, x, y: b.logical_and(x, y),
  opcode.opmap["INPLACE_OR"]: lambda b, x, y: b.logical_or(x, y),
  opcode.opmap["INPLACE_XOR"]: lambda b, x, y: b.logical_xor(x, y),
}

compare_emitters = {
  "<": lambda b, x, y: b.less(x, y),
  "<=": lambda b, x, y: b.less_equal(x, y),
  "==": lambda b, x, y: b.equal(x, y),
  "!=": lambda b, x, y: b.not_equal(x, y),
  ">": lambda b, x, y: b.greater(x, y),
  ">=": lambda b, x, y: b.greater_equal(x, y),
}

# A script object contains the runtime information for a function.
class Script(object):
  def __init__(self, module, func, pyfunc):
    self.module = module
    self.func = func
    self.pyfunc = pyfunc
    self.argcount = 0;
    self.cell = None
    self.argvars = []
    self.argidxs = []
    self.retvar = None
    self.retidx = None

  def link(self, net):
    self.cell = net.cell(self.func.name)
    self.argcount = len(self.argvars)
    for arg in self.argvars:
      idx = None
      if arg in self.cell: idx = self.cell.index(arg)
      self.argidxs.append(idx)
    self.retidx = self.cell.index(self.retvar)

# Module for defining and compiling script functions.
class Module(object):
  def __init__(self):
    self.flow = Flow()
    self.net = None
    self.scripts = {}

  def translate(self, builder, func, args):
    # Add function arguments as variables.
    variables = {}
    bindings = {}
    code = func.__code__
    for i in range(code.co_argcount):
      argname = code.co_varnames[i]
      bindings[args[i]] = argname
      variables[argname] = args[i]

    # Generate Myelin ops from Python byte code.
    stack = []
    retval = None
    for instr in dis.get_instructions(func):
      if instr.opcode == LOAD_GLOBAL:
        name = instr.argval
        value = self.scripts.get(name)
        if value is None:
          value = self.flow.vars.get(name)
        if value is None:
          scope = sys.modules[func.__module__]
          v = getattr(scope, name, None)
          if v is not None:
            value = builder.const(v, name=name)
        if value is None:
          value = function_emitters.get(instr.argval)
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
        variables[varname] = value
        bindings[value] = varname
      elif instr.opcode == CALL_FUNCTION:
        numargs = instr.arg
        params = stack[-numargs:]
        stack = stack[0:-numargs]
        op = stack.pop()
        if type(op) is Script:
          result = self.translate(builder, op.pyfunc, params)
        elif type(op) is str:
          raise Exception("Unknown function " + op)
        else:
          result = op(builder, params)
        stack.append(result)
      elif instr.opcode == RETURN_VALUE:
        retval = stack.pop()
      elif instr.opcode == BUILD_TUPLE:
        size = instr.arg
        value = tuple(stack[-size:])
        stack = stack[0:-size]
        stack.append(value);
      elif instr.opcode == UNPACK_SEQUENCE:
        count = instr.arg
        seq = list(stack.pop())
        for _ in range(count): stack.append(seq.pop())
      elif instr.opcode in binary_emitters:
        y = stack.pop()
        x = stack.pop()
        emitter = binary_emitters[instr.opcode]
        stack.append(emitter(builder, x, y))
      elif instr.opcode in unary_emitters:
        x = stack.pop()
        emitter = unary_emitters[instr.opcode]
        stack.append(emitter(builder, x))
      elif instr.opcode == COMPARE_OP:
        op = instr.argval
        y = stack.pop()
        x = stack.pop()
        emitter = compare_emitters.get(op)
        if emitter is None: raise Exception("Unsupported comparison: " + op)
        result = emitter(builder, x, y)
        stack.append(result)
      elif instr.opcode in inplace_emitters:
        val = stack.pop()
        var = stack.pop()
        emitter = inplace_emitters[instr.opcode]
        result = emitter(builder, var, val)
        varname = bindings[var]
        variables[varname] = result
        bindings[result] = varname
        stack.append(result)
      else:
        raise Exception("Unsupported instruction: " + str(instr))

    return retval

  def build(self, func):
    # Set up builder for function.
    name = func.__name__
    builder = Builder(self.flow, name)

    # Create script object for function.
    script = Script(self, builder.func, func)
    self.scripts[name] = script

    # Create input variables for arguments.
    args = []
    code = func.__code__
    for i in range(code.co_argcount):
      argname = code.co_varnames[i]
      argtype = func.__annotations__[argname]
      var = builder.var(argname, argtype.dt, argtype.dims)
      var.input = True
      var.pyname = argname
      script.argvars.append(var)
      args.append(var)

    # Output Python bytecode.
    if flags.arg.dump_bytecode:
      dis.show_code(func)
      dis.dis(func)

    # Translate Python bytecode to Myelin flow.
    script.retvar = self.translate(builder, func, args)
    if script.retvar:
      if type(script.retvar) is tuple:
        for v in script.retvar: v.output = True
      else:
        script.retvar.output = True

    return script

  # Decorator for turning Python function into a compiled Myelin function
  def function(self, func):
    # Build script from Python function
    script = self.build(func)

    # Trampoline function for running the compiled cell.
    def trampoline(*args):
      # Compile script on-demand.
      if script.cell is None: script.module.compile()

      # Create cell instance.
      data = script.cell.instance()

      # Set up arguments.
      for i in range(script.argcount): data[script.argidxs[i]] = args[i]

      # Execute cell computation.
      data.compute()

      # Return result.
      return data[script.retidx]

    return trampoline

  # Compile and link module.
  def compile(self):
    # Compile flow.
    compiler = api.Compiler()
    self.net = compiler.compile(self.flow)

    # Link scripts to compiled network.
    for script in self.scripts.values():
      script.link(self.net)

  # Add global variables to network.
  def globals(self, globs):
    builder = Builder(self.flow)
    for name, value in globs.items():
      builder.const(value, name=name)

  # Output profiling information if enabled.
  def profile(self):
    if flags.arg.profile:
      print(self.net.profile())

