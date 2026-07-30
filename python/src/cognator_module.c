#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "cognator.h"

static PyObject *CognatorError;

static PyObject *status_error(const char *where, cg_status status) {
    PyErr_Format(CognatorError, "%s: %s", where, cg_status_string(status));
    return NULL;
}

static int apply_string_option(PyObject *value, const char **field) {
    const char *s;
    if (value == NULL || value == Py_None) {
        return 0;
    }
    s = PyUnicode_AsUTF8(value);
    if (s == NULL) {
        return -1;
    }
    *field = s;
    return 0;
}

static int apply_options(PyObject *kwargs, cg_options *options) {
    PyObject *value;
    cg_options_init_defaults(options);
    if (kwargs == NULL) {
        return 0;
    }
    value = PyDict_GetItemString(kwargs, "preset");
    if (value != NULL && value != Py_None) {
        const char *preset = PyUnicode_AsUTF8(value);
        cg_status status;
        if (preset == NULL) return -1;
        status = cg_options_apply_preset(options, preset);
        if (status != CG_OK) {
            status_error("preset", status);
            return -1;
        }
    }
    value = PyDict_GetItemString(kwargs, "threshold");
    if (value != NULL && value != Py_None) {
        options->threshold = PyFloat_AsDouble(value);
        if (PyErr_Occurred()) return -1;
    }
    value = PyDict_GetItemString(kwargs, "threshold_mode");
    if (apply_string_option(value, &options->threshold_mode) < 0) return -1;
    value = PyDict_GetItemString(kwargs, "threshold_min");
    if (value != NULL && value != Py_None) {
        options->threshold_min = PyFloat_AsDouble(value);
        if (PyErr_Occurred()) return -1;
    }
    value = PyDict_GetItemString(kwargs, "threshold_max");
    if (value != NULL && value != Py_None) {
        options->threshold_max = PyFloat_AsDouble(value);
        if (PyErr_Occurred()) return -1;
    }
    value = PyDict_GetItemString(kwargs, "threshold_shrink");
    if (value != NULL && value != Py_None) {
        options->threshold_shrink = PyFloat_AsDouble(value);
        if (PyErr_Occurred()) return -1;
    }
    value = PyDict_GetItemString(kwargs, "bootstrap");
    if (value != NULL && value != Py_None) {
        long parsed = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        options->bootstrap = (int)parsed;
    }
    value = PyDict_GetItemString(kwargs, "seed");
    if (value != NULL && value != Py_None) {
        unsigned long long parsed = PyLong_AsUnsignedLongLong(value);
        if (PyErr_Occurred()) return -1;
        options->seed = (uint64_t)parsed;
    }
    value = PyDict_GetItemString(kwargs, "scorer");
    if (apply_string_option(value, &options->scorer_mode) < 0) return -1;
    value = PyDict_GetItemString(kwargs, "model");
    if (apply_string_option(value, &options->merkmal_system) < 0) return -1;
    value = PyDict_GetItemString(kwargs, "merkmal_system");
    if (apply_string_option(value, &options->merkmal_system) < 0) return -1;
    value = PyDict_GetItemString(kwargs, "min_pairs");
    if (value != NULL && value != Py_None) {
        long parsed = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        options->min_pairs = (int)parsed;
    }
    return 0;
}

static PyObject *sidecar_row_to_dict(const cg_sidecar_row *row) {
    PyObject *dict = PyDict_New();
    if (dict == NULL) return NULL;
#define SET_ITEM(name, value) do { \
    PyObject *item__ = (value); \
    if (item__ == NULL || PyDict_SetItemString(dict, (name), item__) < 0) { \
        Py_XDECREF(item__); \
        Py_DECREF(dict); \
        return NULL; \
    } \
    Py_DECREF(item__); \
} while (0)
    SET_ITEM("ID", PyUnicode_FromString(row->id));
    SET_ITEM("Cognator_ID", PyUnicode_FromString(row->cognator_id));
    SET_ITEM("Cognator_Confidence", PyFloat_FromDouble(row->confidence));
    SET_ITEM("Cognator_Assigned_Support", PyFloat_FromDouble(row->assigned_support));
    SET_ITEM("Cognator_Alternative_Support", PyFloat_FromDouble(row->alternative_support));
    SET_ITEM("Cognator_Support_Margin", PyFloat_FromDouble(row->support_margin));
    SET_ITEM("Cognator_Cluster_Size", PyLong_FromLong(row->cluster_size));
    SET_ITEM("Cognator_Candidate_Neighbors", PyLong_FromLong(row->candidate_neighbors));
    SET_ITEM("Cognator_Bootstrap", PyFloat_FromDouble(row->bootstrap));
    SET_ITEM("Cognator_Bootstrap_Low", PyFloat_FromDouble(row->bootstrap_low));
    SET_ITEM("Cognator_Bootstrap_High", PyFloat_FromDouble(row->bootstrap_high));
#undef SET_ITEM
    return dict;
}

static PyObject *py_detect_file(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *path;
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    cg_result *result = NULL;
    cg_options options;
    PyObject *rows = NULL;
    uint64_t i;
    cg_status status;
    (void)self;

    if (!PyArg_ParseTuple(args, "s:detect_file", &path)) {
        return NULL;
    }
    if (apply_options(kwargs, &options) < 0) {
        return NULL;
    }
    status = cg_context_new_builtin(&ctx);
    if (status != CG_OK) return status_error("context", status);
    status = cg_read_table(path, &records);
    if (status != CG_OK) {
        cg_context_free(ctx);
        return status_error("read_table", status);
    }
    status = cg_detect(ctx, records, &options, &result);
    if (status != CG_OK) {
        cg_records_free(records);
        cg_context_free(ctx);
        return status_error("detect", status);
    }
    rows = PyList_New((Py_ssize_t)cg_result_row_count(result));
    if (rows != NULL) {
        for (i = 0; i < cg_result_row_count(result); i++) {
            PyObject *row = sidecar_row_to_dict(cg_result_row_at(result, i));
            if (row == NULL) {
                Py_DECREF(rows);
                rows = NULL;
                break;
            }
            PyList_SET_ITEM(rows, (Py_ssize_t)i, row);
        }
    }
    cg_result_free(result);
    cg_records_free(records);
    cg_context_free(ctx);
    return rows;
}

static PyObject *py_version(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    (void)self;
    return PyUnicode_FromString(cg_version_string());
}

static PyObject *py_abi_version(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    (void)self;
    return PyLong_FromUnsignedLong((unsigned long)cg_abi_version());
}

static PyMethodDef CognatorMethods[] = {
    {"detect_file", (PyCFunction)(void (*)(void))py_detect_file, METH_VARARGS | METH_KEYWORDS, "Run detection on a TSV/CSV file."},
    {"version", py_version, METH_NOARGS, "Return the linked C library version."},
    {"abi_version", py_abi_version, METH_NOARGS, "Return the linked C ABI version."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef CognatorModule = {
    PyModuleDef_HEAD_INIT,
    "_cognator",
    NULL,
    -1,
    CognatorMethods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC PyInit__cognator(void) {
    PyObject *module = PyModule_Create(&CognatorModule);
    if (module == NULL) return NULL;
    CognatorError = PyErr_NewException("cognator.Error", NULL, NULL);
    if (CognatorError == NULL) {
        Py_DECREF(module);
        return NULL;
    }
    Py_INCREF(CognatorError);
    if (PyModule_AddObject(module, "Error", CognatorError) < 0) {
        Py_DECREF(CognatorError);
        Py_DECREF(module);
        return NULL;
    }
    return module;
}
