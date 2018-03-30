/*

 Lunatic Python
 --------------
 
 Copyright (c) 2002-2005  Gustavo Niemeyer <gustavo@niemeyer.net>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/
#include <Python.h>

/* need this to build with Lua 5.2: enables lua_strlen() macro */
#define LUA_COMPAT_ALL

#include <lua.h>
#include <luaconf.h>
#include <lauxlib.h>
#include <lualib.h>

#include "pythoninlua.h"
#include "luainpython.h"

static PyObject *LuaObject_New(lua_State *L, int n)
{
    LuaObject *obj = PyObject_New(LuaObject, &LuaObject_Type);
    if (obj)
    {
        lua_pushvalue(L, n);
        obj->ref = luaL_ref(L, LUA_REGISTRYINDEX);
        obj->refiter = 0;
        obj->L = L;
    }
    return (PyObject *)obj;
}

PyObject *LuaConvert(lua_State *L, int n)
{

    PyObject *ret = NULL;

    switch (lua_type(L, n))
    {

    case LUA_TNIL:
        Py_INCREF(Py_None);
        ret = Py_None;
        break;

    case LUA_TSTRING:
    {
        size_t len;
        const char *s = lua_tolstring(L, n, &len);
        ret = PyUnicode_FromStringAndSize(s, len);
        if (!ret)
        {
            PyErr_Clear();
            ret = PyBytes_FromStringAndSize(s, len);
        }
        break;
    }

    case LUA_TNUMBER:
    {
        lua_Number num = lua_tonumber(L, n);
        if (num != (long)num)
        {
            ret = PyFloat_FromDouble(num);
        }
        else
        {
            ret = PyLong_FromLong((long)num);
        }
        break;
    }

    case LUA_TBOOLEAN:
        ret = lua_toboolean(L, n) ? Py_True : Py_False;
        Py_INCREF(ret);
        break;

    case LUA_TUSERDATA:
    {
        py_object *obj = luaPy_to_pobject(L, n);

        if (obj)
        {
            Py_INCREF(obj->o);
            ret = obj->o;
            break;
        }

        /* Otherwise go on and handle as custom. */
    }

    default:
        ret = LuaObject_New(L, n);
        break;
    }

    return ret;
}

static PyObject *LuaCall(lua_State *L, PyObject *args)
{
    PyObject *ret = NULL;
    PyObject *arg;
    int nargs, rc, i;

    if (!PyTuple_Check(args))
    {
        PyErr_SetString(PyExc_TypeError, "tuple expected");
        lua_settop(L, 0);
        return NULL;
    }

    nargs = PyTuple_Size(args);
    for (i = 0; i != nargs; i++)
    {
        arg = PyTuple_GetItem(args, i);
        if (arg == NULL)
        {
            PyErr_Format(PyExc_TypeError,
                         "failed to get tuple item #%d", i);
            lua_settop(L, 0);
            return NULL;
        }
        rc = py_convert(L, arg);
        if (!rc)
        {
            PyErr_Format(PyExc_TypeError,
                         "failed to convert argument #%d", i);
            lua_settop(L, 0);
            return NULL;
        }
    }

    if (lua_pcall(L, nargs, LUA_MULTRET, 0) != 0)
    {
        PyErr_Format(PyExc_Exception,
                     "error: %s", lua_tostring(L, -1));
        return NULL;
    }

    nargs = lua_gettop(L);
    if (nargs == 1)
    {
        ret = LuaConvert(L, 1);
        if (!ret)
        {
            PyErr_SetString(PyExc_TypeError,
                            "failed to convert return");
            lua_settop(L, 0);
            Py_DECREF(ret);
            return NULL;
        }
    }
    else if (nargs > 1)
    {
        ret = PyTuple_New(nargs);
        if (!ret)
        {
            PyErr_SetString(PyExc_RuntimeError,
                            "failed to create return tuple");
            lua_settop(L, 0);
            return NULL;
        }
        for (i = 0; i != nargs; i++)
        {
            arg = LuaConvert(L, i + 1);
            if (!arg)
            {
                PyErr_Format(PyExc_TypeError,
                             "failed to convert return #%d", i);
                lua_settop(L, 0);
                Py_DECREF(ret);
                return NULL;
            }
            PyTuple_SetItem(ret, i, arg);
        }
    }
    else
    {
        Py_INCREF(Py_None);
        ret = Py_None;
    }

    lua_settop(L, 0);

    return ret;
}

static void LuaObject_dealloc(LuaObject *self)
{
    lua_State* L = self->L;
    luaL_unref(L, LUA_REGISTRYINDEX, self->ref);
    if (self->refiter)
        luaL_unref(L, LUA_REGISTRYINDEX, self->refiter);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *LuaObject_getattr(PyObject *obj, PyObject *attr)
{
    lua_State *L = ((LuaObject *)obj)->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ((LuaObject *)obj)->ref);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        PyErr_SetString(PyExc_RuntimeError, "lost reference");
        return NULL;
    }

    if (!lua_isstring(L, -1) && !lua_istable(L, -1) && !lua_isuserdata(L, -1))
    {
        lua_pop(L, 1);
        PyErr_SetString(PyExc_RuntimeError, "not an indexable value");
        return NULL;
    }

    PyObject *ret = NULL;
    int rc = py_convert(L, attr);
    if (rc)
    {
        lua_gettable(L, -2);
        ret = LuaConvert(L, -1);
    }
    else
    {
        PyErr_SetString(PyExc_ValueError, "can't convert attr/key");
    }
    lua_settop(L, 0);
    return ret;
}

static int LuaObject_setattr(PyObject *obj, PyObject *attr, PyObject *value)
{
    int ret = -1;
    int rc;
    lua_State *L = ((LuaObject *)obj)->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ((LuaObject *)obj)->ref);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        PyErr_SetString(PyExc_RuntimeError, "lost reference");
        return -1;
    }
    if (!lua_istable(L, -1))
    {
        lua_pop(L, -1);
        PyErr_SetString(PyExc_TypeError, "Lua object is not a table");
        return -1;
    }
    rc = py_convert(L, attr);
    if (rc)
    {
        if (NULL == value)
        {
            lua_pushnil(L);
            rc = 1;
        }
        else
        {
            rc = py_convert(L, value);
        }

        if (rc)
        {
            lua_settable(L, -3);
            ret = 0;
        }
        else
        {
            PyErr_SetString(PyExc_ValueError,
                            "can't convert value");
        }
    }
    else
    {
        PyErr_SetString(PyExc_ValueError, "can't convert key/attr");
    }
    lua_settop(L, 0);
    return ret;
}

static PyObject *LuaObject_str(PyObject *obj)
{
    PyObject *ret = NULL;
    const char *s;
    lua_State *L = ((LuaObject *)obj)->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ((LuaObject *)obj)->ref);
    if (luaL_callmeta(L, -1, "__tostring"))
    {
        s = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (s)
            ret = PyUnicode_FromString(s);
    }
    if (!ret)
    {
        int type = lua_type(L, -1);
        switch (type)
        {
        case LUA_TTABLE:
        case LUA_TFUNCTION:
            ret = PyUnicode_FromFormat("<Lua %s at %p>",
                                       lua_typename(L, type),
                                       lua_topointer(L, -1));
            break;

        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            ret = PyUnicode_FromFormat("<Lua %s at %p>",
                                       lua_typename(L, type),
                                       lua_touserdata(L, -1));
            break;

        case LUA_TTHREAD:
            ret = PyUnicode_FromFormat("<Lua %s at %p>",
                                       lua_typename(L, type),
                                       (void *)lua_tothread(L, -1));
            break;

        default:
            ret = PyUnicode_FromFormat("<Lua %s>",
                                       lua_typename(L, type));
            break;
        }
    }
    lua_pop(L, 1);
    return ret;
}

#if LUA_VERSION_NUM == 501
enum
{
    LUA_OK,
    LUA_OPEQ,
    LUA_OPLT,
    LUA_OPLE,
};
static int lua_compare(int lhs, int rhs, int op)
{
    lua_State *L = ((LuaObject *)lhs)->L;
    switch (op)
    {
    case LUA_OPEQ:
        return lua_equal(L, lhs, rhs);
    case LUA_OPLT:
        return lua_lessthan(L, lhs, rhs);
    case LUA_OPLE:
        return lua_lessthan(L, lhs, rhs) || lua_equal(L, lhs, rhs);
    }
    return 0;
}
#endif
static int LuaObject_pcmp(lua_State *L)
{
    int op = lua_tointeger(L, -3);
    switch (op)
    {
    case Py_EQ:
        lua_pushboolean(L, lua_compare(L, -2, -1, LUA_OPEQ));
        break;
    case Py_NE:
        lua_pushboolean(L, !lua_compare(L, -2, -1, LUA_OPEQ));
        break;
    case Py_GT:
        lua_insert(L, -2);
    case Py_LT:
        lua_pushboolean(L, lua_compare(L, -2, -1, LUA_OPLT));
        break;
    case Py_GE:
        lua_insert(L, -2);
    case Py_LE:
        lua_pushboolean(L, lua_compare(L, -2, -1, LUA_OPLE));
    }

    return 1;
}

static PyObject *LuaObject_richcmp(PyObject *lhs, PyObject *rhs, int op)
{
    if (!LuaObject_Check(rhs))
        return Py_False;
    lua_State *L = ((LuaObject *)lhs)->L;
    lua_pushcfunction(L, LuaObject_pcmp);
    lua_pushinteger(L, op);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ((LuaObject *)lhs)->ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ((LuaObject *)rhs)->ref);
    if (lua_pcall(L, 3, 1, 0) != LUA_OK)
    {
        PyErr_SetString(PyExc_RuntimeError, lua_tostring(L, -1));
        return NULL;
    }
    return lua_toboolean(L, -1) ? Py_True : Py_False;
}

static PyObject *LuaObject_call(PyObject *obj, PyObject *args)
{
    lua_State* L = ((LuaObject*)obj)->L;
    lua_settop(L, 0);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ((LuaObject *)obj)->ref);
    return LuaCall(L, args);
}

static PyObject *LuaObject_iternext(LuaObject *obj)
{
    PyObject *ret = NULL;
    lua_State* L = ((LuaObject*)obj)->L;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ((LuaObject *)obj)->ref);

    if (obj->refiter == 0)
        lua_pushnil(L);
    else
        lua_rawgeti(L, LUA_REGISTRYINDEX, obj->refiter);

    if (lua_next(L, -2) != 0)
    {
        /* Remove value. */
        lua_pop(L, 1);
        ret = LuaConvert(L, -1);
        /* Save key for next iteration. */
        if (!obj->refiter)
            obj->refiter = luaL_ref(L, LUA_REGISTRYINDEX);
        else
            lua_rawseti(L, LUA_REGISTRYINDEX, obj->refiter);
    }
    else if (obj->refiter)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, obj->refiter);
        obj->refiter = 0;
    }

    return ret;
}

static int LuaObject_length(LuaObject *obj)
{
    int len;
    lua_State* L = ((LuaObject*)obj)->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ((LuaObject *)obj)->ref);
    len = luaL_len(L, -1);
    lua_settop(L, 0);
    return len;
}

static PyObject *LuaObject_subscript(PyObject *obj, PyObject *key)
{
    return LuaObject_getattr(obj, key);
}

static int LuaObject_ass_subscript(PyObject *obj,
                                   PyObject *key, PyObject *value)
{
    return LuaObject_setattr(obj, key, value);
}

static PyMappingMethods LuaObject_as_mapping = {
#if PY_VERSION_HEX >= 0x02050000
    (lenfunc)LuaObject_length, /*mp_length*/
#else
    (inquiry)LuaObject_length, /*mp_length*/
#endif
    (binaryfunc)LuaObject_subscript,        /*mp_subscript*/
    (objobjargproc)LuaObject_ass_subscript, /*mp_ass_subscript*/
};

PyTypeObject LuaObject_Type = {
    PyVarObject_HEAD_INIT(NULL, 0) "lua.custom", /*tp_name*/
    sizeof(LuaObject),                           /*tp_basicsize*/
    0,                                           /*tp_itemsize*/
    (destructor)LuaObject_dealloc,               /*tp_dealloc*/
    0,                                           /*tp_print*/
    0,                                           /*tp_getattr*/
    0,                                           /*tp_setattr*/
    0,                                           /*tp_compare*/
    LuaObject_str,                               /*tp_repr*/
    0,                                           /*tp_as_number*/
    0,                                           /*tp_as_sequence*/
    &LuaObject_as_mapping,                       /*tp_as_mapping*/
    0,                                           /*tp_hash*/
    (ternaryfunc)LuaObject_call,                 /*tp_call*/
    LuaObject_str,                               /*tp_str*/
    LuaObject_getattr,                           /*tp_getattro*/
    LuaObject_setattr,                           /*tp_setattro*/
    0,                                           /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,    /*tp_flags*/
    "custom lua object",                         /*tp_doc*/
    0,                                           /*tp_traverse*/
    0,                                           /*tp_clear*/
    LuaObject_richcmp,                           /*tp_richcompare*/
    0,                                           /*tp_weaklistoffset*/
    PyObject_SelfIter,                           /*tp_iter*/
    (iternextfunc)LuaObject_iternext,            /*tp_iternext*/
    0,                                           /*tp_methods*/
    0,                                           /*tp_members*/
    0,                                           /*tp_getset*/
    0,                                           /*tp_base*/
    0,                                           /*tp_dict*/
    0,                                           /*tp_descr_get*/
    0,                                           /*tp_descr_set*/
    0,                                           /*tp_dictoffset*/
    0,                                           /*tp_init*/
    PyType_GenericAlloc,                         /*tp_alloc*/
    PyType_GenericNew,                           /*tp_new*/
    PyObject_Del,                                /*tp_free*/
    0,                                           /*tp_is_gc*/
};

PyObject *Lua_run(PyObject *args, int eval)
{
    PyObject *ret;
    char *buf = NULL;
    char *s;
    int len;
    lua_State *L = NULL;

    if (!PyArg_ParseTuple(args, "ks#", &L, &s, &len))
        return NULL;

    lua_settop(L, 0);

    if (eval)
    {
        buf = (char *)malloc(strlen("return ") + len + 1);
        strcpy(buf, "return ");
        strncat(buf, s, len);
        s = buf;
        len = strlen("return ") + len;
    }

    if (luaL_loadbuffer(L, s, len, "<python>") != 0)
    {
        PyErr_Format(PyExc_RuntimeError,
                     "error loading code: %s",
                     lua_tostring(L, -1));
        return NULL;
    }

    free(buf);
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0)
    {
        PyErr_Format(PyExc_RuntimeError,
                     "error executing code: %s",
                     lua_tostring(L, -1));
        return NULL;
    }

    int nargs, i;
    nargs = lua_gettop(L);
    if (nargs == 1)
    {
        ret = LuaConvert(L, 1);
        if (!ret)
        {
            PyErr_SetString(PyExc_TypeError,
                            "failed to convert return");
            lua_settop(L, 0);
            Py_DECREF(ret);
            return NULL;
        }
    }
    else if (nargs > 1)
    {
        ret = PyTuple_New(nargs);
        if (!ret)
        {
            PyErr_SetString(PyExc_RuntimeError,
                            "failed to create return tuple");
            lua_settop(L, 0);
            return NULL;
        }
        for (i = 0; i != nargs; i++)
        {
            PyObject *arg = LuaConvert(L, i + 1);
            if (!arg)
            {
                PyErr_Format(PyExc_TypeError,
                             "failed to convert return #%d", i);
                lua_settop(L, 0);
                Py_DECREF(ret);
                return NULL;
            }
            PyTuple_SetItem(ret, i, arg);
        }
    }
    else
    {
        Py_INCREF(Py_None);
        ret = Py_None;
    }

    lua_settop(L, 0);
    return ret;
}

PyObject *Lua_newState()
{
    lua_State *L = luaL_newstate();
    if (L != NULL)
    {
        luaL_openlibs(L);
        luaopen_python(L);

        lua_pop(L, 1);

        lua_settop(L, 0);
    }
    PyObject *ret = Py_BuildValue("k", L);
    Py_INCREF(ret);
    return ret;
}

PyObject *Lua_closeState(PyObject *self, PyObject *args)
{
    lua_State *L = NULL;
    if (PyArg_ParseTuple(args, "k", &L))
    {
        lua_close(L);
    }
    Py_INCREF(Py_None);
    return Py_None;
}

PyObject *Lua_execute(PyObject *self, PyObject *args)
{
    return Lua_run(args, 0);
}

PyObject *Lua_eval(PyObject *self, PyObject *args)
{
    return Lua_run(args, 1);
}

PyObject *Lua_globals(PyObject *self, PyObject *args)
{
    lua_State *L = NULL;
    if (PyArg_ParseTuple(args, "k", &L))
    {
        PyObject *ret = NULL;
        lua_getglobal(L, "_G");
        if (lua_isnil(L, -1))
        {
            PyErr_SetString(PyExc_RuntimeError,
                            "lost globals reference");
            lua_pop(L, 1);
            return NULL;
        }
        ret = LuaConvert(L, -1);
        if (!ret)
            PyErr_Format(PyExc_TypeError,
                         "failed to convert globals table");
        lua_settop(L, 0);
        return ret;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *Lua_require(PyObject *self, PyObject *args)
{
    lua_State *L = NULL;
    if (PyArg_ParseTuple(args, "k", &L))
    {
        lua_getglobal(L, "require");
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            PyErr_SetString(PyExc_RuntimeError, "require is not defined");
            return NULL;
        }
        return LuaCall(L, args);
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef lua_methods[] =
    {
        {"new", Lua_newState, METH_VARARGS, NULL},
        {"close", Lua_closeState, METH_VARARGS, NULL},
        {"execute", Lua_execute, METH_VARARGS, NULL},
        {"eval", Lua_eval, METH_VARARGS, NULL},
        {"globals", Lua_globals, METH_VARARGS, NULL},
        {"require", Lua_require, METH_VARARGS, NULL},
        {NULL, NULL}};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef lua_module =
    {
        PyModuleDef_HEAD_INIT,
        "lua",
        "Lunatic-Python Python-Lua bridge",
        -1,
        lua_methods};
#endif

PyMODINIT_FUNC PyInit_lua(void)
{
    PyObject *m;
    if (PyType_Ready(&LuaObject_Type) < 0 ||
#if PY_MAJOR_VERSION >= 3
        (m = PyModule_Create(&lua_module)) == NULL)
        return NULL;
#else
        (m = Py_InitModule3("lua", lua_methods,
                            "Lunatic-Python Python-Lua bridge")) == NULL)
        return;
#endif

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}
