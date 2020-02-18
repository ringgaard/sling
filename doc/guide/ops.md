# Myelin operations

* [Abs](#abs) (absolute value)
* [Acos](#acos) (inverse cosine)
* [Acosh](#acosh) (inverse hyperbolic cosine)
* [Acot](#acot) (inverse cotangent)
* [Acoth](#acoth) (inverse hyperbolic cotangent)
* [Acsc](#acsc) (inverse cosecant)
* [Acsch](#acsch) (inverse hyperbolic cosecant)
* [Add](#add) (addition)
* [All](#all) (and reduction)
* [And](#and) (logic and)
* [AndNot](#andnot) (logic and-not)
* [Any](#any) (or reduction)
* [ArgMax](#argmax) (maximum argument)
* [ArgMin](#argmin) (minimum argument)
* [Asec](#asec) (inverse secant)
* [Asech](#asech) (inverse hyperbolic secant)
* [Asin](#asin) (inverse sine)
* [Asinh](#asinh) (inverse hyperbolic sine)
* [Assign](#assign) (assignment)
* [Atan](#atan) (inverse tangent)
* [Atanh](#atanh) inverse hyperbolic tangent)
* [Broadcast](#broadcast) (broadcast argument)
* [Calculate](#calculate) (calculate expression)
* [Ceil](#ceil) (round up towards infinity)
* [Concat](#concat) (concatenate tensors)
* [Cond](#cond) (conditional computation)
* [Cos](#cos) (cosine)
* [CosDist](#cosdist) (cosine distance)
* [Cosh](#cosh) (hyperbolic cosine)
* [CosSim](#cossim) (cosine similarity)
* [Cot](#cot) (cotangent)
* [Coth](#coth) (hyperbolic cotangent)
* [Count](#count) (predicate reduction)
* [Csc](#csc) (cosecant)
* [Csch](#csch) (hyperbolic cosecant)
* [Div](#div) (division)
* [DotProduct](#dotproduct) (dot product)
* [Equal](#equal) (compare equal)
* [Erf](#erf) (Gauss error function)
* [Exp](#exp) (exponential)
* [Floor](#floor) (round down towards negative infinity)
* [Gather](#gather) (gather embeddings)
* [GatherAvg](#gatheravg) (gather average embeddings)
* [GatherMax](#gathermax) (gather maximum embeddings)
* [GatherSum](#gathersum) (gather sum of embeddings)
* [Greater](#greater) (compare greater)
* [GreaterEqual](#greaterequal) (compare greater or equal)
* [Identity](#identity) (identity function)
* [IsNegative](#isnegative) (compare negative)
* [IsPositive](#ispositive) (compare positive)
* [IsZero](#iszero) (compare zero)
* [Less](#less) (compare less)
* [LessEqual](#lessequal) (compare less of equal)
* [Log](#log) (natural logarithm)
* [LogSigmoid](#logsigmoid) (log sigmoid)
* [LogSoftmax](#logsoftmax) (log softmax)
* [MatMul](#matmul) (matrix multiplication)
* [Max](#max) (max reduction)
* [Maximum](#maximum) (maximum)
* [Mean](#mean) (mean reduction)
* [Min](#min) (min reduction)
* [Minimum](#minimum) (minimum)
* [Mul](#mul) (product)
* [Neg](#neg) (negation)
* [Norm](#norm) (vector norm)
* [Normalize](#normalize) (normalize norm)
* [Not](#not) (logic negation)
* [NotEqual](#notqqual) (compare not equal)
* [OneHot](#onehot) (one-hot vector)
* [Or](#or) (logic or)
* [Pow](#pow) (power function)
* [Product](#product) (product reduction)
* [Rank](#rank) (tensor rank)
* [Reciprocal](#reciprocal) (reciprocal)
* [Reference](#reference) (tensor reference)
* [Relu](#relu) (rectified linear unit)
* [Reshape](#) (tensor reshaping)
* [Resize](#) (resize tensor by cropping and padding)
* [Round](#round) (round to nearest)
* [Rsqrt](#rsqrt) (reciprocal square root)
* [Scatter](#) (scatter embedding)
* [Sec](#sec) (secant)
* [Sech](#sech) (hyperbolic secant)
* [Select](#select) (conditional select)
* [Shape](#shape) (tensor shape)
* [Sigmoid](#sigmoid) (sigmoid)
* [Sign](#sign) (sign indicator)
* [Sin](#sin) (sine)
* [Sinh](#sinh) (hyperbolic sine)
* [Size](#size) (tensor size)
* [Softmax](#softmax) (softmax)
* [Softplus](#softplus) (softplus)
* [Softsign](#softsign) (softsign)
* [Split](#split) (split tensor)
* [Sqrt](#sqrt) (square root)
* [Square](#square) (square)
* [Sub](#sub) (subtraction)
* [Sum](#sum) (sum reduction)
* [Tan](#tan) (tangent)
* [Tanh](#tanh) (hyperbolic tangent)
* [Transpose](#transpose) (permute dimensions)
* [Trunc](#trunc) (round towards zero)
* [Xor](#xor) (logic exclusive or)

## Math

### Abs

Computes the absolute value of a tensor element-wise.
```
Abs(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Returns:**

A tensor of the same type and shape as `x` with the absolute value, i.e. `|x|`,
of each element of `x`.

------------------------------------------------------------------------------

### Add

Computes the sum of two tensors element-wise.
```
Add(x, y)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.
- `y`: Tensor of same type as `x`.

**Returns:**

A tensor with the sum of `x` and `y`, i.e. `x + y`. This operation supports
broadcasting.

------------------------------------------------------------------------------

### Calculate

Computes one or more expressions over the input tensors.
```
Calculate(x_1, x_2, ...)
```
**Arguments:**
- `x_i`: i'th input tensor. All tensors must be of the same type.

**Attributes:**
- `expr`: Recipe for expression to compute.

**Returns:**

One tensor for each output in the expression. This operation supports
broadcasting.

An expression recipe is a text format for representing computations over
inputs variables to produce the output variables. A recipe has the following
grammar:
```
   <recipe> := <assignment> | <assignment> ';' <recipe>
   <assignment> := <variable> '=' <expression>
   <expression> := <variable> | <operation>
   <operation> := <name> '(' <arg list> ')'
   <arg list> := <arg> | <arg> ',' <arg list>
   <arg> := <variable> | <expression>
   <variable> := <input variable> | <constant> | <register>
                 <output variable> | <temp variable> | <number>
   <input variable> := '%' <integer>
   <constant> := '#' <integer>
   <register> := '!' <integer>
   <output variable> := '@' <integer>
   <temp variable> := '$' <integer>
   <number> := '_' <integer>
```

------------------------------------------------------------------------------

### Div

Computes the division of `x` by `y` element-wise.
```
Div(x, y)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.
- `y`: Tensor of same type as `x`.

**Returns:**

A tensor with the division of `x` by `y`, i.e. `x / y`. This operation supports
broadcasting.

------------------------------------------------------------------------------

### Erf

Computes the Gauss error function of `x` element-wise.
```
Erf(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with Gauss error function value,
i.e. `erf(x)`, of each element of `x`.

------------------------------------------------------------------------------

### Exp

Computes exponential of `x` element-wise.
```
Exp(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the exponential,
i.e. `exp(x)=e^x`, of each element of `x`.

------------------------------------------------------------------------------

### Log

Computes natural logarithm of `x` element-wise.
```
Log(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the natural logarithm,
i.e. `log(x)`, of each element of `x`.

------------------------------------------------------------------------------

### LogSigmoid

Computes log sigmoid of `x` element-wise.
```
LogSigmoid(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the log sigmoid,
i.e. `log(1 / (1 + exp(-x)))`, of each element of `x`.

------------------------------------------------------------------------------

### Maximum

Computes the maximum of two tensors element-wise.
```
Maximum(x, y)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.
- `y`: Tensor of same type as `x`.

**Returns:**

A tensor with the maximum of `x` and `y`, i.e. `max(x, y)` or `x > y ? x : y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Minimum

Computes the minimum of two tensors element-wise.
```
Minimum(x, y)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.
- `y`: Tensor of same type as `x`.

**Returns:**

A tensor with the minimum of `x` and `y`, i.e. `min(x, y)` or `x < y ? x : y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Mul

Computes the product of two tensors element-wise.
```
Mul(x, y)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.
- `y`: Tensor of same type as `x`.

**Returns:**

A tensor with the Hadamard product of `x` and `y`, i.e. `x * y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Neg

Computes the negative value of a tensor element-wise.
```
Neg(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Returns:**

A tensor of the same type and shape as `x` with the negative value, i.e. `-x`,
of each element of `x`.

------------------------------------------------------------------------------

### Pow

Computes the power of one value to another element-wise.
```
Pow(x, y)
```
**Arguments:**
- `x`: Tensor of type float32, float64.
- `y`: Tensor of same type as `x`.

**Returns:**

A tensor with `x` raised to the power of `y`, i.e. `x^y` or `e^(y * log(x))`.
This operation supports broadcasting.
If `y` is one of the following constants, the operation is replaced by
a more specific operation:

|  y   | operation      |
|-----:|----------------|
|  0   | 1              |
|  1   | x              |
|  2   | Square(x)      |
|  0.5 | Sqrt(x)        |
| -0.5 | Rsqrt(x)       |
| -1   | Reciprocal(x)  |

------------------------------------------------------------------------------

### Reciprocal

Computes the reciprocal value of a tensor element-wise.
```
Reciprocal(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the reciprocal value,
i.e. `1 / x`, of each element of `x`.

------------------------------------------------------------------------------

### Relu

Computes the rectified linear value of a tensor element-wise.
```
Relu(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Returns:**

A tensor of the same type and shape as `x` with the rectified linear value,
i.e. `max(x, 0)`, of each element of `x`.

------------------------------------------------------------------------------

### Rsqrt

Computes the reciprocal of square root of a tensor element-wise.
```
Rsqrt(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the reciprocal of square root,
i.e. `1 / sqrt(x)`, of each element of `x`.

------------------------------------------------------------------------------

### Sigmoid

Computes the sigmoid of a tensor element-wise.
```
Sigmoid(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the sigmoid,
i.e. `1 / (1 + exp(-x))`, of each element of `x`.

------------------------------------------------------------------------------

### Sign

Computes the indication of sign of a tensor element-wise.
```
Sign(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the sign indicator,
i.e. `sign(x) = -1 if x < 0; 0 if x = 0; 1 if x > 0`, of each element of `x`.

------------------------------------------------------------------------------

### Softplus

Computes the softplus function of a tensor element-wise.
```
Softplus(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the softplus value,
i.e. `log(e^x + 1)`, of each element of `x`.

------------------------------------------------------------------------------

### Softsign

Computes the softsign function of a tensor element-wise.
```
Softsign(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the softsign value,
i.e. `x / (|x| + 1)`, of each element of `x`.

------------------------------------------------------------------------------

### Sqrt

Computes the square root of a tensor element-wise.
```
Sqrt(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the square root,
i.e. `sqrt(x)=x^0.5`, of each element of `x`.

------------------------------------------------------------------------------

### Square

Computes the square of a tensor element-wise.
```
Square(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Returns:**

A tensor of the same type and shape as `x` with the square,
i.e. `x^2=x*x`, of each element of `x`.

------------------------------------------------------------------------------

### Sub

Computes the difference of two tensors element-wise.
```
Sub(x, y)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.
- `y`: Tensor of same type as `x`.

**Returns:**

A tensor with the sum of `x` and `y`, i.e. `x - y`. This operation supports
broadcasting.

------------------------------------------------------------------------------

### Softmax

Computes the softmax of a tensor element-wise.
```
Softmax(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the softmax,
i.e. `exp(x) / sum(exp(x))`, of each element of `x`.

This macro operation is implemented as
`Softmax(x)=Normalize(Exp(Sub(x, Max(x))))`

------------------------------------------------------------------------------

### LogSoftmax

Computes the log-softmax of a tensor element-wise.
```
LogSoftmax(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the log-softmax,
i.e. `log(exp(x) / sum(exp(x)))`, of each element of `x`.

This macro operation is implemented as
`LogSoftmax(x)=Log(Softmax(x))`


## Rounding

### Ceil

Rounds tensor up towards infinity element-wise.
```
Ceil(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with each element rounded to the
smallest integer not less than `x`.

------------------------------------------------------------------------------

### Floor

Rounds tensor down towards negative infinity element-wise.
```
Floor(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with each element rounded to the
largest integer not greater than `x`.

------------------------------------------------------------------------------

### Round

Rounds tensor to nearest integer element-wise.
```
Round(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with each element rounded to the
nearest integer of `x`.

------------------------------------------------------------------------------

### Trunc

Rounds tensor towards zero element-wise.
```
Round(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with each element rounded towards
zero.


## Trigonometry

### Acos

Computes inverse cosine of a tensor element-wise.
```
Acos(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse cosine of each
element of `x`.

------------------------------------------------------------------------------

### Acosh

Computes inverse hyperbolic cosine of a tensor element-wise.
```
Acosh(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse hyperbolic cosine
of each element of `x`.

------------------------------------------------------------------------------

### Acot

Computes inverse cotangent of a tensor element-wise.
```
Acot(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse cotangent
of each element of `x`.

------------------------------------------------------------------------------

### Acoth

Computes inverse hyperbolic cotangent of a tensor element-wise.
```
Acoth(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse hyperbolic cotangent
of each element of `x`.

------------------------------------------------------------------------------

### Acsc

Computes inverse cosecant of a tensor element-wise.
```
Acsc(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse cosecant
of each element of `x`.

------------------------------------------------------------------------------

### Acsch

Computes inverse hyperbolic cosecant of a tensor element-wise.
```
Acsch(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse hyperbolic cosecant
of each element of `x`.

------------------------------------------------------------------------------

### Asec

Computes inverse secant of a tensor element-wise.
```
Asec(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse secant
of each element of `x`.

------------------------------------------------------------------------------

### Asech

Computes inverse hyperbolic secant of a tensor element-wise.
```
Asech(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse hyperbolic secant
of each element of `x`.

------------------------------------------------------------------------------

### Asin

Computes inverse sine of a tensor element-wise.
```
Asin(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse sine
of each element of `x`.

------------------------------------------------------------------------------

### Asinh

Computes inverse hyperbolic sine of a tensor element-wise.
```
Asinh(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse hyperbolic sine
of each element of `x`.

------------------------------------------------------------------------------

### Atan

Computes inverse tangent of a tensor element-wise.
```
Atan(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse tangent
of each element of `x`.

------------------------------------------------------------------------------

### Atanh

Computes inverse hyperbolic tangent of a tensor element-wise.
```
Atanh(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the inverse hyperbolic tangent
of each element of `x`.

------------------------------------------------------------------------------

### Cos

Computes cosine of a tensor element-wise.
```
Cos(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the cosine
of each element of `x`.

------------------------------------------------------------------------------

### Cosh

Computes hyperbolic cosine of a tensor element-wise.
```
Cosh(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the hyperbolic cosine
of each element of `x`.

------------------------------------------------------------------------------

### Cot

Computes cotangent of a tensor element-wise.
```
Cot(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the cotangent
of each element of `x`.

------------------------------------------------------------------------------

### Coth

Computes hyperbolic cotangent of a tensor element-wise.
```
Coth(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the hyperbolic cotangent
of each element of `x`.

------------------------------------------------------------------------------

### Csc

Computes cosecant of a tensor element-wise.
```
Csc(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the cosecant
of each element of `x`.

------------------------------------------------------------------------------

### Csch

Computes hyperbolic cosecant of a tensor element-wise.
```
Csch(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the hyperbolic cosecant
of each element of `x`.

------------------------------------------------------------------------------

### Sec

Computes secant of a tensor element-wise.
```
Sec(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the secant
of each element of `x`.

------------------------------------------------------------------------------

### Sech

Computes hyperbolic secant of a tensor element-wise.
```
Sech(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the hyperbolic secant
of each element of `x`.

------------------------------------------------------------------------------

### Sin

Computes sine of a tensor element-wise.
```
Sin(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the sine
of each element of `x`.

------------------------------------------------------------------------------

### Sinh

Computes hyperbolic sine of a tensor element-wise.
```
Sinh(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the hyperbolic sine
of each element of `x`.

------------------------------------------------------------------------------

### Tan

Computes tangent of a tensor element-wise.
```
Tan(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the tangent
of each element of `x`.

------------------------------------------------------------------------------

### Tanh

Computes hyperbolic tangent of a tensor element-wise.
```
Tanh(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A tensor of the same type and shape as `x` with the hyperbolic tangent
of each element of `x`.

## Reductions

### All

Computes logical AND of predicates across a dimension of a tensor.
```
All(x)
```
**Arguments:**
- `x`: Predcate tensor of type float32, float64, int8, int16, int32, or int64.

**Attributes:**
- `axis`: The dimension to reduce. If missing, `x` is reduced over all
dimensions.
- `keepdims`: If true, keep reduced dimension with length 1.

**Returns:**

A reduced tensor of the same type as `x`. Reduces `x` along the dimension given
in `axis` (or all dimensions). If `keepdims` is true, the reduced dimension is
retained with length 1. Otherwise, the rank of the tensor is reduced by 1, or
reduced to a scalar if reducing over all dimensions.

------------------------------------------------------------------------------

### Any

Computes logical OR of predicates across a dimension of a tensor.
```
Any(x)
```
**Arguments:**
- `x`: Predcate tensor of type float32, float64, int8, int16, int32, or int64.

**Attributes:**
- `axis`: The dimension to reduce. If missing, `x` is reduced over all
dimensions.
- `keepdims`: If true, keep reduced dimension with length 1.

**Returns:**

A reduced tensor of the same type as `x`. Reduces `x` along the dimension given
in `axis` (or all dimensions). If `keepdims` is true, the reduced dimension is
retained with length 1. Otherwise, the rank of the tensor is reduced by 1, or
reduced to a scalar if reducing over all dimensions.

------------------------------------------------------------------------------

### ArgMax

Finds the index with the largest value of a tensor.

```
ArgMax(x)
```
**Arguments:**
- `x`: Tensor of type float32.

**Returns:**

An integer (int32), with the index of the element with the largest value.

------------------------------------------------------------------------------

### ArgMin

Finds the index with the smallest value of a tensor.

```
ArgMin(x)
```
**Arguments:**
- `x`: Tensor of type float32.

**Returns:**

An integer (int32), with the index of the element with the smallest value.

------------------------------------------------------------------------------

### Count

Counts the number of true predicate values of a tensor.

```
Count(x)
```
**Arguments:**
- `x`: Predicate tensor of type float32, float64, int8, int16, int32, or int64.

**Returns:**

An integer (int32), with the count of the number of true predicate values of
`x`. This equivalent to `Sum(Select(x, One()))`.

------------------------------------------------------------------------------

### Max

Computes the maximum value across a dimension of a tensor.
```
Max(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Attributes:**
- `axis`: The dimension to reduce. If missing, `x` is reduced over all
dimensions.
- `keepdims`: If true, keep reduced dimension with length 1.

**Returns:**

A reduced tensor of the same type as `x`. Reduces `x` along the dimension given
in `axis` (or all dimensions). If `keepdims` is true, the reduced dimension is
retained with length 1. Otherwise, the rank of the tensor is reduced by 1, or
reduced to a scalar if reducing over all dimensions.

------------------------------------------------------------------------------

### Mean

Computes the mean value across a dimension of a tensor.
```
Mean(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Attributes:**
- `axis`: The dimension to reduce. If missing, `x` is reduced over all
dimensions.
- `keepdims`: If true, keep reduced dimension with length 1.

**Returns:**

A reduced tensor of the same type as `x`. Reduces `x` along the dimension given
in `axis` (or all dimensions). If `keepdims` is true, the reduced dimension is
retained with length 1. Otherwise, the rank of the tensor is reduced by 1, or
reduced to a scalar if reducing over all dimensions.

This macro operation is implemented as
`Mean(x)=Div(Sum(x), Size(x))`

------------------------------------------------------------------------------

### Min

Computes the minimum value across a dimension of a tensor.
```
Min(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Attributes:**
- `axis`: The dimension to reduce. If missing, `x` is reduced over all
dimensions.
- `keepdims`: If true, keep reduced dimension with length 1.

**Returns:**

A reduced tensor of the same type as `x`. Reduces `x` along the dimension given
in `axis` (or all dimensions). If `keepdims` is true, the reduced dimension is
retained with length 1. Otherwise, the rank of the tensor is reduced by 1, or
reduced to a scalar if reducing over all dimensions.

------------------------------------------------------------------------------

### Product

Computes the product of all values across a dimension of a tensor.
```
Product(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Attributes:**
- `axis`: The dimension to reduce. If missing, `x` is reduced over all
dimensions.
- `keepdims`: If true, keep reduced dimension with length 1.

**Returns:**

A reduced tensor of the same type as `x`. Reduces `x` along the dimension given
in `axis` (or all dimensions). If `keepdims` is true, the reduced dimension is
retained with length 1. Otherwise, the rank of the tensor is reduced by 1, or
reduced to a scalar if reducing over all dimensions.

------------------------------------------------------------------------------

### Sum

Computes the sum of all values across a dimension of a tensor.
```
Sum(x)
```
**Arguments:**
- `x`: Tensor of type float32, float64, int8, int16, int32, or int64.

**Attributes:**
- `axis`: The dimension to reduce. If missing, `x` is reduced over all
dimensions.
- `keepdims`: If true, keep reduced dimension with length 1.

**Returns:**

A reduced tensor of the same type as `x`. Reduces `x` along the dimension given
in `axis` (or all dimensions). If `keepdims` is true, the reduced dimension is
retained with length 1. Otherwise, the rank of the tensor is reduced by 1, or
reduced to a scalar if reducing over all dimensions.


## Comparison

### Cond

Selects value from one of two tensor depending on the truth value of a third
tensor element-wise.
```
Cond(p, x, y)
```
**Arguments:**
- `p`: Predicate tensor of type float32 or float64.
- `x`: Tensor of same type as `p`.
- `y`: Tensor of same type as `p`.

**Returns:**

A tensor of the same type and shape as `x` and `y` which is `x` if `p` is
true, and `y` if `p` is false, i.e. `p ? x : y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Equal

Checks if two tensors are equal element-wise.
```
Equal(x, y)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.
- `y`: Tensor of same type as `x`.

**Returns:**

A predicate tensor of the same type and shape as `x` and `y` which is true
iff `x = y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Greater

Checks if one tensor is greater than another tensor element-wise.
```
Greater(x, y)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.
- `y`: Tensor of same type as `x`.

**Returns:**

A predicate tensor of the same type and shape as `x` and `y` which is true
iff `x > y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### GreaterEqual

Checks if one tensor is greater than or equal to another tensor element-wise.
```
GreaterEqual(x, y)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.
- `y`: Tensor of same type as `x`.

**Returns:**

A predicate tensor of the same type and shape as `x` and `y` which is true
iff `x >= y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Less

Checks if one tensor is less than another tensor element-wise.
```
Less(x, y)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.
- `y`: Tensor of same type as `x`.

**Returns:**

A predicate tensor of the same type and shape as `x` and `y` which is true
iff `x < y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### LessEqual

Checks if one tensor is less than or equal to another tensor element-wise.
```
LessEqual(x, y)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.
- `y`: Tensor of same type as `x`.

**Returns:**

A predicate tensor of the same type and shape as `x` and `y` which is true
iff `x <= y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### NotEqual

Checks if one tensor not equal to another tensor element-wise.
```
NotEqual(x, y)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.
- `y`: Tensor of same type as `x`.

**Returns:**

A predicate tensor of the same type and shape as `x` and `y` which is true
iff `x != y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Select

Selects value from a tensor depending on the truth value a predicate tensor
element-wise.
```
Select(p, x)
```
**Arguments:**
- `p`: Predicate tensor of type float32 or float64.
- `x`: Tensor of same type as `p`.

**Returns:**

A tensor of the same type and shape as `x` which is `x` if `p` is
true, and `0` if `p` is false, i.e. `p ? x : 0`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### IsZero

Checks if tensor is zero element-wise.
```
IsZero(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A predicate tensor of the same type and shape as `x` which is true
iff `x = 0`.

This macro operation is implemented as
`IsZero(x)=Equal(x, 0)`

------------------------------------------------------------------------------

### IsPositive

Checks if tensor is positive element-wise.
```
IsPositive(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A predicate tensor of the same type and shape as `x` which is true
iff `x > 0`.

This macro operation is implemented as
`IsPositive(x)=Greater(x, 0)`

------------------------------------------------------------------------------

### IsNegative

Checks if tensor is negative element-wise.
```
IsNegative(x)
```
**Arguments:**
- `x`: Tensor of type float32 or float64.

**Returns:**

A predicate tensor of the same type and shape as `x` which is true
iff `x < 0`.

This macro operation is implemented as
`IsNegative(x)=Less(x, 0)`


## Logic

### And

Computes logical AND (conjunction) of two predicate tensors element-wise.
```
And(x, y)
```
**Arguments:**
- `x`: Predicate tensor of type float32 or float64.
- `y`: Predicate tensor of same type as `x`.

**Returns:**

A predicate tensor with the logical AND of `x` and `y`, i.e. `x & y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### AndNot

Computes logical ANDNOT of two predicate tensors element-wise.
```
AndNot(x, y)
```
**Arguments:**
- `x`: Predicate tensor of type float32 or float64.
- `y`: Predicate tensor of same type as `x`.

**Returns:**

A predicate tensor with the logical ANDNOT of `x` and `y`, i.e. `!x & y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Not

Computes logical NOT (negation) of tensor element-wise.
```
Not(x)
```
**Arguments:**
- `x`: Predicate tensor of type float32 or float64.

**Returns:**

A predicate tensor with the logical NOT of `x`, i.e. `!x`.

------------------------------------------------------------------------------

### Or

Computes logical OR (disjunction) of two predicate tensors element-wise.
```
Or(x, y)
```
**Arguments:**
- `x`: Predicate tensor of type float32 or float64.
- `y`: Predicate tensor of same type as `x`.

**Returns:**

A predicate tensor with the logical OR of `x` and `y`, i.e. `x | y`.
This operation supports broadcasting.

------------------------------------------------------------------------------

### Xor

Computes logical XOR (exclusive or) of two predicate tensors element-wise.
```
Xor(x, y)
```
**Arguments:**
- `x`: Predicate tensor of type float32 or float64.
- `y`: Predicate tensor of same type as `x`.

**Returns:**

A predicate tensor with the logical XOR of `x` and `y`, i.e. `x ^ y`.
This operation supports broadcasting.


## Sparse

### Gather

Gathers slices of a tensor according to feature vector indices.
```
Gather(m, f, [oov])
```
**Arguments:**
- `m`: Embedding tensor of type float32 with shape [n, v_1, ... v_d],
that contains n embedding vectors with shape [v_1, ... v_d].
- `f`: Feature tensor of type int32 with `p` elements.
- `oov`: Optional tensor of type float32 for out-of-vocabulary vector with
shape [v_1, ..., v_d]. If an element in `f` is ouside the range [0;n-1], the `oov`
vector is used instead of the embedding vector in `m`.

**Returns:**

A float32 tensor with shape [p, v_1, ..., v_d] tensor where the i'th slice
is `m[f[i]]` or `oov` if `f[i]` is not in the range [0;n-1].

------------------------------------------------------------------------------

### GatherAvg

Computes the average of slices of a tensor according to feature vector indices.
```
GatherAvg(m, f)
```
**Arguments:**
- `m`: Embedding tensor of type float32 with shape [n, d],
that contains d embedding vectors of size d.
- `f`: Feature tensor of type int32.

**Returns:**

A float32 tensor with shape [1, d] tensor which is the element-wise average
of the slices `m[f[i]]`.

------------------------------------------------------------------------------

    GatherMax
    GatherSum
    Scatter (MulScatter, AssignAddScatter, AssignAddMulScatter)

## Arrays
    Concat
    Split
    OneHot
    Resize

## Linear algebra

    DotProduct
    CosDist
    CosSim
    MatMul (MatMulAdd, MatMulRelu, MatMulAddRelu)
    Norm
    Normalize
    Transpose

## Shape
    Rank
    Reshape
    Shape
    Size

## Assignment

    Assign (AssignAddMatMul)
    Identity
    Broadcast
    Reference

