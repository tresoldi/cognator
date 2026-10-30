#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "cognator.h"

#include <string.h>

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
        double resolved_threshold = cg_result_threshold(result);
        for (i = 0; i < cg_result_row_count(result); i++) {
            PyObject *row = sidecar_row_to_dict(cg_result_row_at(result, i));
            PyObject *threshold = NULL;
            if (row == NULL) {
                Py_DECREF(rows);
                rows = NULL;
                break;
            }
            threshold = PyFloat_FromDouble(resolved_threshold);
            if (threshold == NULL || PyDict_SetItemString(row, "Cognator_Resolved_Threshold", threshold) < 0) {
                Py_XDECREF(threshold);
                Py_DECREF(row);
                Py_DECREF(rows);
                rows = NULL;
                break;
            }
            Py_DECREF(threshold);
            PyList_SET_ITEM(rows, (Py_ssize_t)i, row);
        }
    }
    cg_result_free(result);
    cg_records_free(records);
    cg_context_free(ctx);
    return rows;
}

static PyObject *pair_row_to_dict(const cg_pair_row *row) {
    static const char *scopes[4] = {"stratum", "lect_pair", "sparse", "none"};
    int level = row->null_level >= 0 && row->null_level <= 3 ? row->null_level : 3;
    return Py_BuildValue(
        "{s:s,s:K,s:s,s:K,s:d,s:d,s:d,s:d,s:d,s:K,s:s}",
        "form_a", row->form_a,
        "morph_a", (unsigned long long)row->morph_a,
        "form_b", row->form_b,
        "morph_b", (unsigned long long)row->morph_b,
        "weight", row->weight,
        "distance", row->distance,
        "global", row->global_distance,
        "local", row->local_distance,
        "overlap", row->overlap_distance,
        "null_n", (unsigned long long)row->null_count,
        "null_scope", scopes[level]);
}

/* Reads the pair-export keys; the rest of kwargs are cg_options. */
static int apply_pairs_spec(PyObject *kwargs, cg_pairs_spec *spec) {
    PyObject *value;
    cg_pairs_spec_init_defaults(spec);
    if (kwargs == NULL) return 0;
    value = PyDict_GetItemString(kwargs, "pairs_unit");
    if (value != NULL && value != Py_None) {
        const char *unit = PyUnicode_AsUTF8(value);
        if (unit == NULL) return -1;
        if (strcmp(unit, "words") == 0) {
            spec->unit = CG_PAIRS_WORDS;
        } else if (strcmp(unit, "morphemes") == 0) {
            spec->unit = CG_PAIRS_MORPHEMES;
        } else {
            PyErr_Format(PyExc_ValueError, "pairs must be 'words' or 'morphemes', not %R", value);
            return -1;
        }
    }
    value = PyDict_GetItemString(kwargs, "morpheme_ids");
    if (value != NULL && value != Py_None) {
        int truth = PyObject_IsTrue(value);
        if (truth < 0) return -1;
        spec->morpheme_clusters = truth;
    }
    if (spec->morpheme_clusters && spec->unit != CG_PAIRS_MORPHEMES) {
        PyErr_SetString(PyExc_ValueError, "morpheme_ids clusters morphemes and needs pairs='morphemes'");
        return -1;
    }
    value = PyDict_GetItemString(kwargs, "null_samples");
    if (value != NULL && value != Py_None) {
        long parsed = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        spec->null_samples = (int)parsed;
    }
    value = PyDict_GetItemString(kwargs, "null_min");
    if (value != NULL && value != Py_None) {
        long parsed = PyLong_AsLong(value);
        if (PyErr_Occurred()) return -1;
        spec->min_null = (int)parsed;
    }
    value = PyDict_GetItemString(kwargs, "null_seed");
    if (value != NULL && value != Py_None) {
        unsigned long long parsed = PyLong_AsUnsignedLongLong(value);
        if (PyErr_Occurred()) return -1;
        spec->null_seed = (uint64_t)parsed;
    }
    return 0;
}

static PyObject *owned_string(cg_status status, char *text, const char *where) {
    PyObject *value;
    if (status != CG_OK) return status_error(where, status);
    value = PyUnicode_FromString(text);
    cg_string_free(text);
    return value;
}

/* detect_file plus the pairwise export: a dict with the sidecar rows, the
 * pair rows, and the TSV and q_pairs JSON exactly as the C writers produce
 * them, so a file written from Python is the file the CLI writes. */
static PyObject *py_detect_file_pairs(PyObject *self, PyObject *args, PyObject *kwargs) {
    const char *path;
    cg_context *ctx = NULL;
    cg_records *records = NULL;
    cg_result *result = NULL;
    cg_options options;
    cg_pairs_spec spec;
    PyObject *out = NULL;
    PyObject *rows = NULL;
    PyObject *pairs = NULL;
    PyObject *item = NULL;
    char *text = NULL;
    uint64_t i;
    cg_status status;
    (void)self;

    if (!PyArg_ParseTuple(args, "s:detect_file_pairs", &path)) return NULL;
    if (apply_options(kwargs, &options) < 0) return NULL;
    if (apply_pairs_spec(kwargs, &spec) < 0) return NULL;
    status = cg_context_new_builtin(&ctx);
    if (status != CG_OK) return status_error("context", status);
    status = cg_read_table(path, &records);
    if (status != CG_OK) {
        cg_context_free(ctx);
        return status_error("read_table", status);
    }
    status = cg_detect_with_pairs(ctx, records, &options, &spec, &result);
    if (status != CG_OK) {
        cg_records_free(records);
        cg_context_free(ctx);
        return status_error("detect", status);
    }
    out = PyDict_New();
    rows = PyList_New((Py_ssize_t)cg_result_row_count(result));
    pairs = PyList_New((Py_ssize_t)cg_result_pair_count(result));
    if (out == NULL || rows == NULL || pairs == NULL) goto fail;
    for (i = 0; i < cg_result_row_count(result); i++) {
        PyObject *row = sidecar_row_to_dict(cg_result_row_at(result, i));
        const char *morpheme_ids = cg_result_morpheme_ids_at(result, i);
        if (row == NULL) goto fail;
        PyList_SET_ITEM(rows, (Py_ssize_t)i, row);
        item = PyFloat_FromDouble(cg_result_threshold(result));
        if (item == NULL || PyDict_SetItemString(row, "Cognator_Resolved_Threshold", item) < 0) goto fail;
        Py_CLEAR(item);
        if (morpheme_ids != NULL) {
            item = PyUnicode_FromString(morpheme_ids);
            if (item == NULL || PyDict_SetItemString(row, "Cognator_Morpheme_IDs", item) < 0) goto fail;
            Py_CLEAR(item);
        }
    }
    for (i = 0; i < cg_result_pair_count(result); i++) {
        PyObject *pair = pair_row_to_dict(cg_result_pair_at(result, i));
        if (pair == NULL) goto fail;
        PyList_SET_ITEM(pairs, (Py_ssize_t)i, pair);
    }
    if (PyDict_SetItemString(out, "rows", rows) < 0 || PyDict_SetItemString(out, "pairs", pairs) < 0) goto fail;
    status = cg_result_pairs_tsv(result, &text);
    item = owned_string(status, text, "pairs_tsv");
    if (item == NULL || PyDict_SetItemString(out, "tsv", item) < 0) goto fail;
    Py_CLEAR(item);
    text = NULL;
    status = cg_result_pairs_json(result, &text);
    item = owned_string(status, text, "pairs_json");
    if (item == NULL || PyDict_SetItemString(out, "json", item) < 0) goto fail;
    Py_CLEAR(item);
    Py_DECREF(rows);
    Py_DECREF(pairs);
    cg_result_free(result);
    cg_records_free(records);
    cg_context_free(ctx);
    return out;
fail:
    Py_XDECREF(item);
    Py_XDECREF(rows);
    Py_XDECREF(pairs);
    Py_XDECREF(out);
    cg_result_free(result);
    cg_records_free(records);
    cg_context_free(ctx);
    return NULL;
}

static PyObject *py_version(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    (void)self;
    return PyUnicode_FromString(cg_version_string());
}

static PyObject *py_abi_version(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    (void)self;
    return PyLong_FromUnsignedLong((unsigned long)cg_abi_version());
}

/* (version, commit) of the merkmal compiled in. A tuple rather than two
 * functions because callers recording provenance want both or neither, and a
 * result annotated with one of them is not reproducible. */
static PyObject *py_merkmal(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    (void)self;
    return Py_BuildValue("(ss)", cg_merkmal_version(), cg_merkmal_commit());
}

static PyMethodDef CognatorMethods[] = {
    {"detect_file", (PyCFunction)(void (*)(void))py_detect_file, METH_VARARGS | METH_KEYWORDS, "Run detection on a TSV/CSV file."},
    {"detect_file_pairs", (PyCFunction)(void (*)(void))py_detect_file_pairs, METH_VARARGS | METH_KEYWORDS, "Run detection with the pairwise export (q(pairs))."},
    {"version", py_version, METH_NOARGS, "Return the linked C library version."},
    {"abi_version", py_abi_version, METH_NOARGS, "Return the linked C ABI version."},
    {"merkmal", py_merkmal, METH_NOARGS, "Return (version, commit) of the merkmal compiled in."},
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
