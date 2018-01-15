// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/pyapi/pytask.h"

using namespace sling::task;

namespace sling {

// Python type declarations.
PyTypeObject PyJob::type;

PyMethodDef PyJob::methods[] = {
  {"run", (PyCFunction) &PyJob::Run, METH_NOARGS, ""},
  {nullptr}
};

void PyJob::Define(PyObject *module) {
  InitType(&type, "sling.api.Job", sizeof(PyJob));

  type.tp_init = reinterpret_cast<initproc>(&PyJob::Init);
  type.tp_dealloc = reinterpret_cast<destructor>(&PyJob::Dealloc);
  type.tp_methods = methods;

  RegisterType(&type, module, "Job");
}

int PyJob::Init(PyObject *args, PyObject *kwds) {
  // Get python job argument.
  PyObject *pyjob = nullptr;
  if (!PyArg_ParseTuple(args, "O", &pyjob)) return -1;

  // Create new job.
  LOG(INFO) << "Create job";
  job_ = new Job();

  // Get resources.
  ResourceMapping resource_mapping;
  PyObject *resources = PyAttr(pyjob, "resources");
  for (int i = 0; i < PyList_Size(resources); ++i) {
    PyObject *pyresource = PyList_GetItem(resources, i);
    PyObject *pyformat = PyAttr(pyresource, "format");
    PyObject *pyshard = PyAttr(pyresource, "shard");

    const char *name = PyStrAttr(pyresource, "name");
    Format format = PyGetFormat(pyformat);
    Shard shard = PyGetShard(pyshard);


    Resource *resource = job_->CreateResource(name, format, shard);
    resource_mapping[pyresource] = resource;
    LOG(INFO) << "Resource " << name << " format: " << format.ToString()
              << " shard: " << shard.part() << "/" << shard.total();

    Py_DECREF(pyformat);
    Py_DECREF(pyshard);
  }
  Py_DECREF(resources);

  // Get tasks.
  TaskMapping task_mapping;
  PyObject *tasks = PyAttr(pyjob, "tasks");
  for (int i = 0; i < PyList_Size(tasks); ++i) {
    PyObject *pytask = PyList_GetItem(tasks, i);
    const char *type = PyStrAttr(pytask, "type");
    const char *name = PyStrAttr(pytask, "name");

    PyObject *pyshard = PyAttr(pytask, "shard");
    Shard shard = PyGetShard(pyshard);
    Py_DECREF(pyshard);

    Task *task = job_->CreateTask(type, name, shard);
    task_mapping[pytask] = task;

    LOG(INFO) << "Task " << name << " type: " << type
              << " shard: " << shard.part() << "/" << shard.total();

    // Get task parameters.
    PyObject *params = PyAttr(pytask, "params");
    Py_ssize_t pos = 0;
    PyObject *k;
    PyObject *v;
    while (PyDict_Next(params, &pos, &k, &v)) {
      const char *key = PyString_AsString(k);
      const char *value = PyString_AsString(v);
      task->AddParameter(key, value);
      LOG(INFO) << "  Param " << key << " = " << value;
    }
    Py_DECREF(params);

    // Bind inputs.
    PyObject *inputs = PyAttr(pytask, "inputs");
    for (int i = 0; i < PyList_Size(inputs); ++i) {
      PyObject *pybinding = PyList_GetItem(inputs, i);
      const char *name = PyStrAttr(pybinding, "name");
      PyObject *pyresource = PyAttr(pybinding, "resource");
      Resource *resource = resource_mapping.at(pyresource);
      CHECK(resource != nullptr);
      job_->BindInput(task, resource, name);
      Py_DECREF(pyresource);
    }
    Py_DECREF(inputs);

    // Bind outputs.
    PyObject *outputs = PyAttr(pytask, "outputs");
    for (int i = 0; i < PyList_Size(outputs); ++i) {
      PyObject *pybinding = PyList_GetItem(outputs, i);
      const char *name = PyStrAttr(pybinding, "name");
      PyObject *pyresource = PyAttr(pybinding, "resource");
      Resource *resource = resource_mapping.at(pyresource);
      CHECK(resource != nullptr);
      job_->BindOutput(task, resource, name);
      Py_DECREF(pyresource);
    }
    Py_DECREF(outputs);
  }
  Py_DECREF(tasks);

  // Get channels.
  PyObject *channels = PyAttr(pyjob, "channels");
  for (int i = 0; i < PyList_Size(channels); ++i) {
    PyObject *pychannel = PyList_GetItem(channels, i);

    PyObject *pyformat = PyAttr(pychannel, "format");
    Format format = PyGetFormat(pyformat);
    Py_DECREF(pyformat);

    PyObject *pyproducer = PyAttr(pychannel, "producer");
    Port producer = PyGetPort(pyproducer, task_mapping);
    Py_DECREF(pyproducer);

    PyObject *pyconsumer = PyAttr(pychannel, "consumer");
    Port consumer = PyGetPort(pyconsumer, task_mapping);
    Py_DECREF(pyconsumer);

    Channel *channel = job_->Connect(producer, consumer, format);

    LOG(INFO) << "Channel " << channel->producer().ToString()
              << " -> " << channel->consumer().ToString()
              << " format: " << channel->format().ToString();
  }
  Py_DECREF(channels);

  return 0;
}

void PyJob::Dealloc() {
  LOG(INFO) << "Destroy job";
  delete job_;
}

PyObject *PyJob::Run() {
  Py_RETURN_NONE;
}

Port PyJob::PyGetPort(PyObject *obj, const TaskMapping &mapping) {
  const char *name = PyStrAttr(obj, "name");

  PyObject *pyshard = PyAttr(obj, "shard");
  Shard shard = PyGetShard(pyshard);
  Py_DECREF(pyshard);

  PyObject *pytask = PyAttr(obj, "task");
  Task *task = mapping.at(pytask);
  Py_DECREF(pytask);

  return Port(task, name, shard);
}

Format PyJob::PyGetFormat(PyObject *obj) {
  const char *file = PyStrAttr(obj, "file");
  const char *key = PyStrAttr(obj, "key");
  const char *value = PyStrAttr(obj, "value");
  return Format(file, key, value);
}

Shard PyJob::PyGetShard(PyObject *obj) {
  if (obj == Py_None) return Shard();
  int part = PyIntAttr(obj, "part");
  int total = PyIntAttr(obj, "total");
  return Shard(part, total);
}

const char *PyJob::PyStrAttr(PyObject *obj, const char *name) {
  PyObject *attr = PyAttr(obj, name);
  const char *str = attr == Py_None ? "" : PyString_AsString(attr);
  CHECK(str != nullptr) << name;
  Py_DECREF(attr);
  return str;
}

int PyJob::PyIntAttr(PyObject *obj, const char *name) {
  PyObject *attr = PyAttr(obj, name);
  int value = PyNumber_AsSsize_t(attr, nullptr);
  Py_DECREF(attr);
  return value;
}

PyObject *PyJob::PyAttr(PyObject *obj, const char *name) {
  PyObject *attr = PyObject_GetAttrString(obj, name);
  CHECK(attr != nullptr) << name;
  return attr;
}

}  // namespace sling

