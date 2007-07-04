//-----------------------------------------------------------------------------
// bsdiff.c
//   Shared library for use by Python. This is derived from bsdiff, the
// standalone utility produced for BSD which can be found at
// http://www.daemonology.net/bsdiff.
//-----------------------------------------------------------------------------

#include <Python.h>

#define MIN(x,y) (((x)<(y)) ? (x) : (y))

//-----------------------------------------------------------------------------
// split()
//-----------------------------------------------------------------------------
static void split(
    off_t *I,
    off_t *V,
    off_t start,
    off_t len,
    off_t h)
{
    off_t i, j, k, x, tmp, jj, kk;

    if (len < 16) {
        for (k = start; k < start + len; k += j) {
            j = 1;
            x = V[I[k] + h];
            for (i = 1; k + i < start + len; i++) {
                if (V[I[k + i] + h] < x) {
                    x = V[I[k + i] + h];
                    j = 0;
                }
                if (V[I[k + i] + h] == x) {
                    tmp = I[k + j];
                    I[k + j] = I[k + i];
                    I[k + i] = tmp;
                    j++;
                }
            }
            for (i = 0; i < j; i++)
                V[I[k +i ]] = k + j -1;
            if (j == 1)
                I[k] = -1;
        }

    } else {

        jj = 0;
        kk = 0;
        x = V[I[start + len / 2] + h];
	for (i = start; i < start + len; i++) {
            if (V[I[i] + h] < x)
                jj++;
            if (V[I[i] + h] == x)
                kk++;
        }
	jj += start;
        kk += jj;

        j = 0;
        k = 0;
        i = start;
        while (i < jj) {
            if (V[I[i] + h] < x) {
                i++;
            } else if (V[I[i] + h] == x) {
                tmp = I[i];
                I[i] = I[jj + j];
                I[jj + j] = tmp;
                j++;
            } else {
                tmp = I[i];
                I[i] = I[kk + k];
                I[kk + k] = tmp;
                k++;
            }
        }

        while (jj + j < kk) {
            if (V[I[jj + j] + h] == x) {
                j++;
            } else {
                tmp = I[jj + j];
                I[jj + j] = I[kk + k];
                I[kk + k] = tmp;
                k++;
            }
        }

        if (jj > start)
            split(I, V, start, jj - start, h);

        for (i = 0; i < kk - jj; i++)
            V[I[jj + i]] = kk - 1;
        if (jj == kk - 1)
            I[jj] = -1;
        if (start + len > kk)
            split(I, V, kk, start + len - kk, h);
    }
}


//-----------------------------------------------------------------------------
// qsufsort()
//-----------------------------------------------------------------------------
static void qsufsort(
    off_t *I,
    off_t *V,
    unsigned char *old,
    off_t oldsize)
{
    off_t buckets[256], i, h, len;

    for (i = 0; i < 256; i++)
        buckets[i] = 0;
    for (i = 0; i < oldsize; i++)
        buckets[old[i]]++;
    for (i = 1; i < 256; i++)
        buckets[i] += buckets[i - 1];
    for (i = 255; i > 0; i--)
        buckets[i] = buckets[i - 1];
    buckets[0] = 0;

    for (i = 0; i < oldsize; i++)
        I[++buckets[old[i]]] = i;
    I[0] = oldsize;
    for (i = 0; i < oldsize; i++)
        V[i] = buckets[old[i]];
    V[oldsize] = 0;
    for (i = 1; i < 256; i++)
        if (buckets[i] == buckets[i - 1] + 1)
            I[buckets[i]] = -1;
    I[0]=-1;

    for (h = 1; I[0] != -(oldsize + 1); h += h) {
        len = 0;
        for (i = 0; i < oldsize + 1;) {
            if (I[i] < 0) {
                len -= I[i];
                i -= I[i];
            } else {
                if (len)
                    I[i - len] =- len;
                len = V[I[i]] + 1 - i;
                split(I, V, i, len, h);
                i += len;
                len=0;
            }
        }
        if (len)
            I[i - len] =- len;
    }

    for (i = 0; i < oldsize + 1; i++)
        I[V[i]] = i;
}


//-----------------------------------------------------------------------------
// matchlen()
//-----------------------------------------------------------------------------
static off_t matchlen(
    unsigned char *old,
    off_t oldsize,
    unsigned char *new,
    off_t newsize)
{
    off_t i;

    for (i = 0; (i < oldsize) && (i < newsize); i++)
        if (old[i] != new[i])
            break;
    return i;
}


//-----------------------------------------------------------------------------
// search()
//-----------------------------------------------------------------------------
static off_t search(
    off_t *I,
    unsigned char *old,
    off_t oldsize,
    unsigned char *new,
    off_t newsize,
    off_t st,
    off_t en,
    off_t *pos)
{
    off_t x, y;

    if (en - st < 2) {
        x = matchlen(old + I[st], oldsize - I[st], new, newsize);
        y = matchlen(old + I[en], oldsize - I[en], new, newsize);

        if (x > y) {
            *pos = I[st];
            return x;
        } else {
            *pos = I[en];
            return y;
        }
    }

    x = st + (en - st) / 2;
    if (memcmp(old + I[x], new, MIN(oldsize - I[x], newsize)) < 0) {
        return search(I, old, oldsize, new, newsize, x, en, pos);
    } else {
        return search(I, old, oldsize, new, newsize, st, x, pos);
    }
}


//-----------------------------------------------------------------------------
// Diff()
//   Performs a diff between the two data streams and returns a tuple
// containing the control, diff and extra blocks that bsdiff produces.
//-----------------------------------------------------------------------------
static PyObject* Diff(
    PyObject* self,			// passthrough argument
    PyObject* args)			// arguments to function
{
    off_t lastscan, lastpos, lastoffset, oldscore, scsc, overlap, Ss, lens;
    off_t *I, *V, dblen, eblen, scan, pos, len, s, Sf, lenf, Sb, lenb, i;
    PyObject *controlTuples, *tuple, *results, *temp;
    int origDataLength, newDataLength;
    char *origData, *newData;
    unsigned char *db, *eb;

    // parse arguments
    if (!PyArg_ParseTuple(args, "s#s#", &origData, &origDataLength, &newData,
            &newDataLength))
        return NULL;

    // create the control tuple
    controlTuples = PyList_New(0);
    if (!controlTuples)
        return NULL;

    // perform sort on original data
    I = PyMem_Malloc((origDataLength + 1) * sizeof(off_t));
    if (!I) {
        Py_DECREF(controlTuples);
        return PyErr_NoMemory();
    }
    V = PyMem_Malloc((origDataLength + 1) * sizeof(off_t));
    if (!V) {
        Py_DECREF(controlTuples);
        PyMem_Free(I);
        return PyErr_NoMemory();
    }
    qsufsort(I, V, origData, origDataLength);
    PyMem_Free(V);

    // allocate memory for the diff and extra blocks
    db = PyMem_Malloc(newDataLength + 1);
    if (!db) {
        Py_DECREF(controlTuples);
        PyMem_Free(I);
        return PyErr_NoMemory();
    }
    eb = PyMem_Malloc(newDataLength + 1);
    if (!eb) {
        Py_DECREF(controlTuples);
        PyMem_Free(I);
        PyMem_Free(db);
        return PyErr_NoMemory();
    }
    dblen = 0;
    eblen = 0;

    // perform the diff
    len = 0;
    scan = 0;
    lastscan = 0;
    lastpos = 0;
    lastoffset = 0;
    while (scan < newDataLength) {
        oldscore = 0;

        for (scsc = scan += len; scan < newDataLength; scan++) {
            len = search(I, origData, origDataLength, newData + scan,
                    newDataLength - scan, 0, origDataLength, &pos);
            for (; scsc < scan + len; scsc++)
                if ((scsc + lastoffset < origDataLength) &&
                        (origData[scsc + lastoffset] == newData[scsc]))
                    oldscore++;
            if (((len == oldscore) && (len != 0)) || (len > oldscore + 8))
                break;
            if((scan + lastoffset < origDataLength) &&
                    (origData[scan + lastoffset] == newData[scan]))
                oldscore--;
        }

        if ((len != oldscore) || (scan == newDataLength)) {
            s = 0;
            Sf = 0;
            lenf = 0;
            for (i = 0; (lastscan + i < scan) &&
                    (lastpos + i < origDataLength);) {
                if (origData[lastpos + i] == newData[lastscan + i])
                    s++;
                i++;
                if (s * 2 - i > Sf * 2 - lenf) {
                    Sf = s;
                    lenf = i;
                }
            }

            lenb = 0;
            if (scan < newDataLength) {
                s = 0;
                Sb = 0;
                for (i = 1; (scan >= lastscan + i) && (pos >= i); i++) {
                    if (origData[pos - i] == newData[scan - i])
                        s++;
                    if (s * 2 - i > Sb * 2 - lenb) {
                        Sb = s;
                        lenb = i;
                    }
                }
            }

            if (lastscan + lenf > scan - lenb) {
                overlap = (lastscan + lenf) - (scan - lenb);
                s = 0;
                Ss = 0;
                lens = 0;
                for (i = 0; i < overlap; i++) {
                    if (newData[lastscan + lenf - overlap + i] ==
                            origData[lastpos + lenf - overlap + i])
                        s++;
                    if (newData[scan - lenb + i]== origData[pos - lenb + i])
                        s--;
                    if (s > Ss) {
                        Ss = s;
                        lens = i + 1;
                    }
                }

                lenf += lens - overlap;
                lenb -= lens;
            }

            for (i = 0; i < lenf; i++)
                db[dblen + i] = newData[lastscan + i] - origData[lastpos + i];
            for (i = 0; i < (scan - lenb) - (lastscan + lenf); i++)
                eb[eblen + i] = newData[lastscan + lenf + i];

            dblen += lenf;
            eblen += (scan - lenb) - (lastscan + lenf);

            tuple = PyTuple_New(3);
            if (!tuple) {
                Py_DECREF(controlTuples);
                PyMem_Free(I);
                PyMem_Free(db);
                PyMem_Free(eb);
                return NULL;
            }
            PyTuple_SET_ITEM(tuple, 0, PyInt_FromLong(lenf));
            PyTuple_SET_ITEM(tuple, 1,
                    PyInt_FromLong((scan - lenb) - (lastscan + lenf)));
            PyTuple_SET_ITEM(tuple, 2,
                    PyInt_FromLong((pos - lenb) - (lastpos + lenf)));
            if (PyList_Append(controlTuples, tuple) < 0) {
                Py_DECREF(controlTuples);
                Py_DECREF(tuple);
                PyMem_Free(I);
                PyMem_Free(db);
                PyMem_Free(eb);
                return NULL;
            }
            Py_DECREF(tuple);

            lastscan = scan - lenb;
            lastpos = pos - lenb;
            lastoffset = pos - scan;
        }
    }

    PyMem_Free(I);
    results = PyTuple_New(3);
    if (!results) {
        PyMem_Free(db);
        PyMem_Free(eb);
        return NULL;
    }
    PyTuple_SET_ITEM(results, 0, controlTuples);
    temp = PyString_FromStringAndSize(db, dblen);
    PyMem_Free(db);
    if (!temp) {
        PyMem_Free(eb);
        Py_DECREF(results);
        return NULL;
    }
    PyTuple_SET_ITEM(results, 1, temp);
    temp = PyString_FromStringAndSize(eb, eblen);
    PyMem_Free(eb);
    if (!temp) {
        Py_DECREF(results);
        return NULL;
    }
    PyTuple_SET_ITEM(results, 2, temp);

    return results;
}


//-----------------------------------------------------------------------------
// Patch()
//   Takes the original data and the control, diff and extra blocks produced
// by bsdiff and returns the new data.
//-----------------------------------------------------------------------------
static PyObject* Patch(
    PyObject* self,			// passthrough argument
    PyObject* args)			// arguments to function
{
    char *origData, *newData, *diffBlock, *extraBlock, *diffPtr, *extraPtr;
    int origDataLength, newDataLength, diffBlockLength, extraBlockLength;
    PyObject *controlTuples, *tuple, *results;
    off_t oldpos, newpos, x, y, z;
    int i, j, numTuples;

    // parse arguments
    if (!PyArg_ParseTuple(args, "s#iO!s#s#", &origData, &origDataLength,
            &newDataLength, &PyList_Type, &controlTuples, &diffBlock,
            &diffBlockLength, &extraBlock, &extraBlockLength))
        return NULL;

    // allocate the memory for the new data
    newData = PyMem_Malloc(newDataLength + 1);
    if (!newData)
        return PyErr_NoMemory();

    oldpos = 0;
    newpos = 0;
    diffPtr = diffBlock;
    extraPtr = extraBlock;
    numTuples = PyList_GET_SIZE(controlTuples);
    for (i = 0; i < numTuples; i++) {
        tuple = PyList_GET_ITEM(controlTuples, i);
        if (!PyTuple_Check(tuple)) {
            PyMem_Free(newData);
            PyErr_SetString(PyExc_TypeError, "expecting tuple");
            return NULL;
        }
        if (PyTuple_GET_SIZE(tuple) != 3) {
            PyMem_Free(newData);
            PyErr_SetString(PyExc_TypeError, "expecting tuple of size 3");
            return NULL;
        }
        x = PyInt_AsLong(PyTuple_GET_ITEM(tuple, 0));
        y = PyInt_AsLong(PyTuple_GET_ITEM(tuple, 1));
        z = PyInt_AsLong(PyTuple_GET_ITEM(tuple, 2));
        if (newpos + x > newDataLength ||
                diffPtr + x > diffBlock + diffBlockLength ||
                extraPtr + y > extraBlock + extraBlockLength) {
            PyMem_Free(newData);
            PyErr_SetString(PyExc_ValueError, "corrupt patch (overflow)");
            return NULL;
        }
        memcpy(newData + newpos, diffPtr, x);
        diffPtr += x;
        for (j = 0; j < x; j++)
            if ((oldpos + j >= 0) && (oldpos + j < origDataLength))
                newData[newpos + j] += origData[oldpos + j];
        newpos += x;
        oldpos += x;
        memcpy(newData + newpos, extraPtr, y);
        extraPtr += y;
        newpos += y;
        oldpos += z;
    }

    // confirm that a valid patch was applied
    if (newpos != newDataLength ||
            diffPtr != diffBlock + diffBlockLength ||
            extraPtr != extraBlock + extraBlockLength) {
        PyMem_Free(newData);
        PyErr_SetString(PyExc_ValueError, "corrupt patch (underflow)");
        return NULL;
    }

    results = PyString_FromStringAndSize(newData, newDataLength);
    PyMem_Free(newData);
    return results;
}


//-----------------------------------------------------------------------------
//   Declaration of methods supported by this module
//-----------------------------------------------------------------------------
static PyMethodDef g_ModuleMethods[] = {
    { "Diff", Diff, METH_VARARGS },
    { "Patch", Patch, METH_VARARGS },
    { NULL, NULL }
};

//-----------------------------------------------------------------------------
// initbsdiff()
//   Initialization routine for the shared libary.
//-----------------------------------------------------------------------------
void initbsdiff(void)
{
    PyObject *module, *moduleDict;

    // initialize module and retrieve the dictionary
    module = Py_InitModule("bsdiff", g_ModuleMethods);
    if (!module)
        return;
    moduleDict = PyModule_GetDict(module);
    if (!moduleDict)
        return;

    // set version for easier support
    if (PyDict_SetItemString(moduleDict, "version",
            PyString_FromString("1.0")) < 0)
        return;

}

