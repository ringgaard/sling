#include <python2.7/Python.h>

#include "sling/api/frames.h"
#include "sling/base/logging.h"

namespace sling {

PyTypeObject PyStore::type;

#if 0
static PyTypeObject noddy_NoddyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "noddy.Noddy",             /* tp_name */
    sizeof(noddy_NoddyObject), /* tp_basicsize */
    0,                         /* tp_itemsize */
    0,                         /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    "Noddy objects",           /* tp_doc */
};
#endif

void PyStore::Define(PyObject *module) {
  PyTypeObject *type = &PyStore::type;
  type->tp_new = PyType_GenericNew;
  PyType_Ready(type);
  Py_INCREF(type);
  PyModule_AddObject(module, "Store", type);
}

}  // namespace sling

