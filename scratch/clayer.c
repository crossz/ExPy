/*
  Copyright (c) Bojan Nikolic <bojan@bnikolic.co.uk> 2010, 2013

  License: See the LICENSE file distributed with this git repository
  
  This file contains the C-language layer of ExPy, namely the function
  which dispatches arbitary UDFs into the python layer.
  
*/

#include <assert.h>
#include <windows.h>
#include <stdio.h>
#include <olectl.h>

#include "xlcall.h"

void SystemError(int error, char *msg)
{
	char Buffer[1024];
	int n;

	if (error) {
		LPVOID lpMsgBuf;
		FormatMessage( 
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM,
			NULL,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&lpMsgBuf,
			0,
			NULL 
			);
		strncpy(Buffer, lpMsgBuf, sizeof(Buffer));
		LocalFree(lpMsgBuf);
	} else
		Buffer[0] = '\0';
	n = lstrlen(Buffer);
	_snprintf(Buffer+n, sizeof(Buffer)-n, msg);
	MessageBox(GetFocus(), Buffer, NULL, MB_OK | MB_ICONSTOP);
}


static char* expy_funcs[2][7] = {
  {"ExPyEvalSS",  "CC#",    "ExPyEvalSS"},
  {"ExPyScript",  "CC#",    "ExPyScript"}
};

XLOPER* new_xlstring(const char* text)
{
    size_t len;

    if(!text || 
       !(len = strlen(text)))
       return NULL;

    XLOPER* x = (XLOPER *)malloc(sizeof(XLOPER) + len + 2);
    if (!x) 
      return 0;
    char* p = (char*)(((char*) x) + sizeof(XLOPER));
    memcpy(p + 1, text, len + 1);
    p[0] = (BYTE)len;
    x->xltype = xltypeStr;
    x->val.str = p;
    return x;
}

__declspec(dllexport) int __stdcall xlAutoOpen(void)
{
  check_init();
  static XLOPER   xDLL;
  int i;

  Excel4(xlGetName, &xDLL, 0);
  static XLOPER xRegister;
  xRegister.xltype = xltypeStr;
  
  for (i=0; i<2; i++)
  {
            XLOPER* fn = new_xlstring(expy_funcs[i][0]);
            XLOPER* ret = new_xlstring(expy_funcs[i][1]);
            XLOPER* alias = new_xlstring(expy_funcs[i][2]);
            BOOL success = Excel4(xlfRegister, 0, 4, &xDLL, fn, ret, alias) == xlretSuccess;
            free(fn); free(ret); free(alias);
        }

    /* Free the XLL filename */
    Excel4(xlFree, 0, 1, &xDLL);

    return 1;
}

__declspec(dllexport) __stdcall char * _ExPyEval(const char *cmd, int stoken)
{
  PyGILState_STATE state;
  PyObject *m=NULL, *d=NULL;
  check_init();
  state = PyGILState_Ensure();

  m = PyImport_ImportModule("__main__");

  if (m) d = PyModule_GetDict(m);

  if (d) {
    PyObject *res = PyRun_String(cmd, 
				 stoken,
				 d, d);
    if (! res)
    {
      PyErr_Print();
    }
    char *rres=PyString_AsString(PyObject_Str(res));
    /* Note that the count of the PyObject_Str object is not
       decremented here*/
    Py_XDECREF(res);
    Py_XDECREF(m);
    return rres;
  } 
  else 
  {
    SystemError(1, "could not import __main__");
  }
  return NULL;
}

__declspec(dllexport) __stdcall char * ExPyScript(const char *cmd)
{
  return _ExPyEval(cmd, 256);
}

__declspec(dllexport) __stdcall char * ExPyEvalSS(const char *cmd)
{
  return _ExPyEval(cmd, 258);
}

/**
 */

__declspec(dllexport) __stdcall LPXLOPER ExPyDispatch(LPXLOPER p1,
						      ...)
{
  XLOPER xCaller;
  int status;
  char formula[256];

  status=Excel4(xlfCaller,
		&xCaller,
		0); 
  XLOPER xCell;
  if ( xCaller.xltype == xltypeSRef ||   xCaller.xltype == xltypeRef )
  {
      status=Excel4(xlfGetFormula,
		  &xCell,
		  1,
		  &xCaller ); 
      size_t len=xCell.val.str[0];
      memcpy(formula, 
	     xCell.val.str+1, 
	     len);
      formula[len]=0;
  }
  else
  {
     MessageBox(GetFocus(), 
		"This function can only be called from the spreadsheet" ,  
		"Error ", 
		MB_OK | MB_ICONSTOP);
     return NULL;
  }
  

  PyObject *m = PyImport_ImportModule("ExPy");

  PyObject *PyNArgs = PyObject_GetAttrString(m, "nArgs");
  PyObject *PyNArgsRes = PyObject_CallFunction(PyNArgs, "s", formula);
  int nargs;
  if (PyArg_ParseTuple(PyNArgsRes, "i", &nargs))
  {
    // do nothing
  }
  else
  {
    printf("Could not convert");
  }

  PyObject * dispArgs=NULL;
  if (nargs>1)
  {
    char fmt[nargs];
    size_t i;
    for(i=0; i<nargs-1; ++i)
      fmt[i]='i';
    fmt[nargs-1]=0;
    va_list vl;
    va_start(vl, p1);
    dispArgs=Py_VaBuildValue(fmt, vl);
    if (dispArgs==NULL)
    {
      PyErr_Print();
    }
  }

  PyObject *PyDispatch = PyObject_GetAttrString(m, "dispatch");
  if (PyDispatch==NULL)
  {
    PyErr_Print();
    return NULL;
  }
  PyObject *PyDispRes;
  if (nargs==0)
  {
    PyDispRes  = PyObject_CallFunction(PyDispatch, "s", formula);
  }
  else if (nargs==1)
  {
    PyDispRes  = PyObject_CallFunction(PyDispatch, "si", formula, p1);
  }
  else
  {
    PyDispRes  = PyObject_CallFunction(PyDispatch, "siO", formula, p1, dispArgs);
  }

  if (PyDispRes==NULL)
  {
    PyErr_Print();
  }

  LPXLOPER res;
  if (PyArg_ParseTuple(PyDispRes, "i", &res))
  {
    // do nothing
  }
  else
  {
    PyErr_Print();
  }

  Py_XDECREF(m);
  Py_XDECREF(PyNArgs);
  if (dispArgs)
  {
    Py_XDECREF(dispArgs);
  }
  Py_XDECREF(PyDispatch);
  Py_XDECREF(PyDispRes);

  return res;
  
}

