/*
 * interpreter.c
 *
 *  Created on: Apr 24, 2015
 *      Author: max
 */

#include "interpreter.h"

static PyThreadState *mainPythonThread;

void interpreterGlobalLoad() {

	Py_Initialize();
	PyEval_InitThreads();
	mainPythonThread = PyEval_SaveThread();

}

void interpreterGlobalUnload() {

	PyEval_RestoreThread(mainPythonThread);
	Py_Finalize();

}

void interpreterCreate(Interpreter *instance) {

	assert(instance != NULL);

	bzero(instance, sizeof(*instance));

}

void interpreterPrepare(Interpreter *interpreter) {

	//PyEval_AcquireLock();

	//interpreter->threadPythonState = Py_NewInterpreter();
	interpreter->threadPythonState = PyThreadState_New(mainPythonThread->interp);

	PyEval_RestoreThread(interpreter->threadPythonState);
	interpreter->pdict = PyDict_New();

	PyDict_SetItemString(interpreter->pdict, "__builtins__", PyEval_GetBuiltins(  ));

	PyEval_SaveThread();
	//PyEval_ReleaseThread(interpreter->threadPythonState);

}

void interpreterAddVariable(Interpreter *interpreter, char *key, char *val) {

	PyObject *pyVal;

	//PyEval_AcquireThread(interpreter->threadPythonState);
	PyEval_RestoreThread(interpreter->threadPythonState);

	pyVal = PyString_FromString(val);
	PyDict_SetItemString(interpreter->pdict, key, pyVal);

	PyEval_SaveThread();
	//PyEval_ReleaseThread(interpreter->threadPythonState) ;
}

int interpreterGetConditionValue(Interpreter *interpreter, char *condition) {

	char* bufferCondition, *cval;
	int result;

	//PyEval_AcquireThread(interpreter->threadPythonState);

	PyEval_RestoreThread(interpreter->threadPythonState);

	bufferCondition = (char*)  malloc(sizeof(char) * 2048);

	memset(bufferCondition, 0, sizeof bufferCondition);

	sprintf(bufferCondition, "result = str(%s)", condition);

	PyRun_String(bufferCondition,
			Py_file_input,
			interpreter->pdict,
			interpreter->pdict);

	interpreter->pval = PyDict_GetItemString(interpreter->pdict, "result");

	PyArg_Parse(interpreter->pval, "s", &cval);

	result = (cval != NULL && strcmp("True", cval) == 0);

	free(bufferCondition);

	//PyEval_ReleaseThread(interpreter->threadPythonState) ;
	PyEval_SaveThread();

	return result;
}

void interpreterFree(Interpreter *interpreter) {

	//PyEval_AcquireThread(interpreter->threadPythonState);
	//PyEval_RestoreThread(interpreter->threadPythonState);

	PyEval_RestoreThread(interpreter->threadPythonState);

	Py_DECREF(interpreter->pdict);

	//Py_EndInterpreter(interpreter->threadPythonState);
	PyThreadState_Clear(interpreter->threadPythonState);

	PyEval_SaveThread();

	//PyEval_SaveThread();
	//PyEval_ReleaseLock();

}
