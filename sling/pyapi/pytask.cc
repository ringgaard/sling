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

#include <unordered_map>

#include "sling/pyapi/pytask.h"

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
  job_ = new task::Job();

  // Get resources.
  std::unordered_map<PyObject *, task::Resource *> resource_mapping;
  PyObject *resources = PyAttr(pyjob, "resources");
  for (int i = 0; i < PyList_Size(resources); ++i) {
    PyObject *resource = PyList_GetItem(resources, i);
    const char *name = PyStrAttr(resource, "name");
    task::Format format = PyGetFormat(PyAttr(resource, "format"));
    task::Shard shard = PyGetShard(PyAttr(resource, "shard"));
    task::Resource *r = job_->CreateResource(name, format, shard);
    resource_mapping[resource] = r;
    LOG(INFO) << "Resource " << name << " format: " << format.ToString()
              << " shard: " << shard.part() << "/" << shard.total();
  }
  Py_DECREF(resources);

  // Get tasks.
  std::unordered_map<PyObject *, task::Task *> task_mapping;
  PyObject *tasks = PyAttr(pyjob, "tasks");
  for (int i = 0; i < PyList_Size(tasks); ++i) {
    PyObject *task = PyList_GetItem(tasks, i);
    const char *type = PyStrAttr(task, "type");
    const char *name = PyStrAttr(task, "name");
    task::Shard shard = PyGetShard(PyAttr(task, "shard"));
    task::Task *t = job_->CreateTask(type, name, shard);
    task_mapping[task] = t;

    LOG(INFO) << "Task " << name << " type: " << type
              << " shard: " << shard.part() << "/" << shard.total();

    // Get task parameters.
    PyObject *params = PyAttr(task, "params");
    Py_ssize_t pos = 0;
    PyObject *k;
    PyObject *v;
    while (PyDict_Next(params, &pos, &k, &v)) {
      const char *key = PyString_AsString(k);
      const char *value = PyString_AsString(v);
      t->AddParameter(key, value);
      LOG(INFO) << "  Param " << key << " = " << value;
    }
    Py_DECREF(params);

    // Bind inputs.
    PyObject *inputs = PyAttr(task, "inputs");
    for (int i = 0; i < PyList_Size(inputs); ++i) {
      PyObject *binding = PyList_GetItem(inputs, i);
      const char *name = PyStrAttr(binding, "name");
      PyObject *resource = PyAttr(binding, "resource");
      task::Resource *r = resource_mapping[resource];
      CHECK(r != nullptr);
      job_->BindInput(t, r, name);
      Py_DECREF(resource);
    }
    Py_DECREF(inputs);

    // Bind outputs.
    PyObject *outputs = PyAttr(task, "outputs");
    for (int i = 0; i < PyList_Size(outputs); ++i) {
      PyObject *binding = PyList_GetItem(outputs, i);
      const char *name = PyStrAttr(binding, "name");
      PyObject *resource = PyAttr(binding, "resource");
      task::Resource *r = resource_mapping[resource];
      CHECK(r != nullptr);
      job_->BindOutput(t, r, name);
      Py_DECREF(resource);
    }
    Py_DECREF(outputs);
  }
  Py_DECREF(tasks);

  return 0;
}

void PyJob::Dealloc() {
  LOG(INFO) << "Destroy job";
  delete job_;
}

PyObject *PyJob::Run() {
  Py_RETURN_NONE;
}

task::Format PyJob::PyGetFormat(PyObject *obj) {
  const char *file = PyStrAttr(obj, "file");
  const char *key = PyStrAttr(obj, "key");
  const char *value = PyStrAttr(obj, "value");
  return task::Format(file, key, value);
}

task::Shard PyJob::PyGetShard(PyObject *obj) {
  if (obj == Py_None) return task::Shard();
  int part = PyIntAttr(obj, "part");
  int total = PyIntAttr(obj, "total");
  return task::Shard(part, total);
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

