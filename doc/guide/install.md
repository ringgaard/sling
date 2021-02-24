# SLING Installation and Building

Operating system: Linux<br>
Languages: C++ (gcc or clang), Python 3.6+, assembler<br>
CPU: Intel x64 or compatible<br>
Build system: Bazel<br>

## Python

If you just want to use SLING from Python, e.g. want to try out the parser on a
pre-trained model, you can install the SLING wheel with pip. On a Linux machine
with Python 3 you can install a pre-built wheel:

```shell
sudo -H pip3 install https://ringgaard.com/data/dist/sling-3.0.0-py3-none-linux_x86_64.whl
```

You can test the installation by trying to import the `sling` package:
```shell
python3 -c "import sling; sling.which()"
```

If you want to try out the SLING parser you can download a
pre-trained model from [Ringgaard Research](https://ringgaard.com):
```
sling fetch --dataset caspar
```

You can then use the parser in Python:
```python
import sling

parser = sling.Parser("data/e/caspar/caspar.flow")

text = input("text: ")
doc = parser.parse(text)
print(doc.frame.data(pretty=True))
for m in doc.mentions:
  print("mention", doc.phrase(m.begin, m.end))
```

## C++

If you want to use SLING for C++ development, you need to download the source
code and build it.

First, clone the GitHub repository.

```shell
git clone https://github.com/google/sling.git
cd sling
```

Next, run the `setup.sh` script to set up the SLING development environment
and build the code:
```shell
./setup.sh
```

This will perform the following steps:
* Install missing package dependencies, notably GCC and Python 3.
* Install [Bazel](https://bazel.build/) which is used as the build system for
  SLING.
* Build SLING from source.
* Set up link to the SLING development enviroment for SLING Python 3 API.

## Building


You can use the `buildall.sh` script to build all the source code:

```shell
tools/buildall.sh
```

If you haven't run the `setup.sh` script already, you then need to link the
sling Python module directly to the Python source directory to use it in
"developer mode":

```shell
sudo ln -s $(realpath python) /usr/lib/python3/dist-packages/sling
```

