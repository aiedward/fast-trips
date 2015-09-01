#include <Python.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>

#include "pathfinder.h"
#include <string>
#include <queue>

static PyObject *pyError;

// global variable
fasttrips::PathFinder pathfinder;

static PyObject *
_fasttrips_initialize_supply(PyObject *self, PyObject *args)
{
    PyArrayObject *pyo;
    const char* output_dir;
    int proc_num;
    PyObject *input1, *input2, *input3, *input4, *input5, *input6;
    if (!PyArg_ParseTuple(args, "siOOOOOO", &output_dir, &proc_num, &input1, &input2,
                          &input3, &input4, &input5, &input6)) {
        return NULL;
    }

    // printf("_fasttrips_initialize_supply for output_dir %s proc num %d\n", output_dir, proc_num);
    // access_links index: TAZ id, stop id
    pyo             = (PyArrayObject*)PyArray_ContiguousFromObject(input1, NPY_INT32, 2, 2);
    if (pyo == NULL) return NULL;
    int* acc_indexes= (int*)PyArray_DATA(pyo);
    int num_indexes = PyArray_DIMS(pyo)[0];
    assert(2 == PyArray_DIMS(pyo)[1]);

    // access_links cost: time, access cost, egress cost
    pyo             = (PyArrayObject*)PyArray_ContiguousFromObject(input2, NPY_DOUBLE, 2, 2);
    if (pyo == NULL) return NULL;
    double* costs   = (double*)PyArray_DATA(pyo);
    int num_costs   = PyArray_DIMS(pyo)[0];
    assert(3 == PyArray_DIMS(pyo)[1]);

    // these better be the same length
    assert(num_indexes == num_costs);

    // trip stop times index: trip id, sequence, stop id
    pyo                 = (PyArrayObject*)PyArray_ContiguousFromObject(input3, NPY_INT32, 2, 2);
    if (pyo == NULL) return NULL;
    int* stop_indexes   = (int*)PyArray_DATA(pyo);
    int num_stop_ind    = PyArray_DIMS(pyo)[0];
    assert(3 == PyArray_DIMS(pyo)[1]);

    // trip stop times data: arrival time, departure time
    pyo                 = (PyArrayObject*)PyArray_ContiguousFromObject(input4, NPY_DOUBLE, 2, 2);
    if (pyo == NULL) return NULL;
    double* stop_times  = (double*)PyArray_DATA(pyo);
    int num_stop_times  = PyArray_DIMS(pyo)[0];
    assert(2 == PyArray_DIMS(pyo)[1]);

    // these better be the same length
    assert(num_stop_ind == num_stop_times);

    // stop transfers index: from stop id, to stop id
    pyo                 = (PyArrayObject*)PyArray_ContiguousFromObject(input5, NPY_INT32, 2, 2);
    if (pyo == NULL) return NULL;
    int* xfer_indexes   = (int*)PyArray_DATA(pyo);
    int num_xfer_ind    = PyArray_DIMS(pyo)[0];
    assert(2 == PyArray_DIMS(pyo)[1]);

    // stop transfers data: time, cost
    pyo                 = (PyArrayObject*)PyArray_ContiguousFromObject(input6, NPY_DOUBLE, 2, 2);
    if (pyo == NULL) return NULL;
    double* xfer_data  = (double*)PyArray_DATA(pyo);
    int num_xfer_data  = PyArray_DIMS(pyo)[0];
    assert(2 == PyArray_DIMS(pyo)[1]);

    // these better be the same length
    assert(num_xfer_ind == num_xfer_data);

    // keep them
    pathfinder.initializeSupply(output_dir, proc_num,
                                acc_indexes, costs, num_indexes,
                                stop_indexes, stop_times, num_stop_ind,
                                xfer_indexes, xfer_data, num_xfer_ind);

    Py_RETURN_NONE;
}

static PyObject *
_fasttrips_set_bump_wait(PyObject* self, PyObject *args)
{
    PyObject *input1, *input2;
    if (!PyArg_ParseTuple(args, "OO", &input1, &input2)) {
        return NULL;
    }
    PyArrayObject *pyo;

    // bump wait index: trip id, stop sequence, stop id
    pyo             = (PyArrayObject*)PyArray_ContiguousFromObject(input1, NPY_INT32, 2, 2);
    if (pyo == NULL) return NULL;
    int* bw_index = (int*)PyArray_DATA(pyo);
    int num_bw    = PyArray_DIMS(pyo)[0];
    assert(3 == PyArray_DIMS(pyo)[1]);

    // bump wait data: arrival time
    pyo             = (PyArrayObject*)PyArray_ContiguousFromObject(input2, NPY_DOUBLE, 1, 1);
    if (pyo == NULL) return NULL;
    double* bw_times= (double*)PyArray_DATA(pyo);
    int num_times   = PyArray_DIMS(pyo)[0];
    assert(num_times == num_bw);

    pathfinder.setBumpWait(bw_index, bw_times, num_bw);
    Py_RETURN_NONE;
}

static PyObject *
_fasttrips_find_path(PyObject *self, PyObject *args)
{
    PyArrayObject *pyo;
    fasttrips::PathSpecification path_spec;
    int   hyperpath_i, outbound_i, trace_i;
    if (!PyArg_ParseTuple(args, "iiiiiidi", &path_spec.passenger_id_, &path_spec.path_id_, &hyperpath_i,
                          &path_spec.origin_taz_id_, &path_spec.destination_taz_id_,
                          &outbound_i, &path_spec.preferred_time_, &trace_i)) {
        return NULL;
    }
    path_spec.hyperpath_  = (hyperpath_i != 0);
    path_spec.outbound_   = (outbound_i  != 0);
    path_spec.trace_      = (trace_i     != 0);

    std::map<int, fasttrips::StopState> path_states;
    std::vector<int> path_stops;
    pathfinder.findPath(path_spec, path_states, path_stops);

    // package for returning.  We'll separate ints and doubles.
    npy_intp dims_int[2];
    dims_int[0] = path_stops.size();
    dims_int[1] = 5; // stop_id, deparr_mode_, stop_succpred_, seq_, seq_succpred_
    PyArrayObject *ret_int = (PyArrayObject *)PyArray_SimpleNew(2, dims_int, NPY_INT32);

    npy_intp dims_double[2];
    dims_double[0] = path_stops.size();
    dims_double[1] = 5; // label_, deparr_time_, link_time_, cost_, arrdep_time_
    PyArrayObject *ret_double = (PyArrayObject *)PyArray_SimpleNew(2, dims_double, NPY_DOUBLE);

    for (int ind = 0; ind < dims_int[0]; ++ind) {
        int stop_id = path_stops[ind];
        *(npy_int32*)PyArray_GETPTR2(ret_int, ind, 0) = stop_id;
        *(npy_int32*)PyArray_GETPTR2(ret_int, ind, 1) = path_states[stop_id].deparr_mode_;
        *(npy_int32*)PyArray_GETPTR2(ret_int, ind, 2) = path_states[stop_id].stop_succpred_;
        *(npy_int32*)PyArray_GETPTR2(ret_int, ind, 3) = path_states[stop_id].seq_;
        *(npy_int32*)PyArray_GETPTR2(ret_int, ind, 4) = path_states[stop_id].seq_succpred_;

        *(npy_double*)PyArray_GETPTR2(ret_double, ind, 0) = path_states[stop_id].label_;
        *(npy_double*)PyArray_GETPTR2(ret_double, ind, 1) = path_states[stop_id].deparr_time_;
        *(npy_double*)PyArray_GETPTR2(ret_double, ind, 2) = path_states[stop_id].link_time_;
        *(npy_double*)PyArray_GETPTR2(ret_double, ind, 3) = path_states[stop_id].cost_;
        *(npy_double*)PyArray_GETPTR2(ret_double, ind, 4) = path_states[stop_id].arrdep_time_;
    }

    PyObject *returnobj = Py_BuildValue("(OO)",ret_int,ret_double);
    return returnobj;
}

static PyMethodDef fasttripsMethods[] = {
    {"initialize_supply",   _fasttrips_initialize_supply, METH_VARARGS, "Initialize network supply" },
    {"set_bump_wait",       _fasttrips_set_bump_wait,     METH_VARARGS, "Update bump wait"          },
    {"find_path",           _fasttrips_find_path,         METH_VARARGS, "Find trip-based path"      },
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC
init_fasttrips(void)
{
    // printf("init_fasttrips called\n");
    std::priority_queue<std::string> myqueue;

    PyObject *m = Py_InitModule("_fasttrips", fasttripsMethods);
    if (m == NULL)
        return;

    import_array();

    pyError = PyErr_NewException("_fasttrips.error", NULL, NULL);
    Py_INCREF(pyError);
    PyModule_AddObject(m, "error", pyError);
}

int
main(int argc, char *argv[])
{
    /* Pass argv[0] to the Python interpreter */
    Py_SetProgramName(argv[0]);

    /* Initialize the Python interpreter.  Required. */
    Py_Initialize();

    /* Add a static module */
    init_fasttrips();
}