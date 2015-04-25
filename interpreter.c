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

Interpreter *interpreterCreate() {

	Interpreter *instance;

	instance = (Interpreter*) malloc(sizeof(*instance));
	assert(instance != NULL);

	bzero(instance, sizeof(*instance));

	return instance;
}

void interpreterPrepare(Interpreter *interpreter) {

	PyEval_AcquireLock();

	interpreter->threadPythonState = Py_NewInterpreter();
	interpreter->pdict = PyDict_New();
	PyDict_SetItemString(interpreter->pdict, "__builtins__", PyEval_GetBuiltins(  ));

	PyEval_ReleaseThread(interpreter->threadPythonState);

}

void interpreterAddVariable(Interpreter *interpreter, char *key, char *val) {

	PyDict_SetItemString(interpreter->pdict, key, PyString_FromString(val));

}

int interpreterGetConditionValue(Interpreter *interpreter, char *condition) {

	char bufferCondition[1024], *cval;
	int result;

	memset(bufferCondition, 0, sizeof bufferCondition);

	sprintf(bufferCondition, "result = str(%s)", condition);

	PyEval_AcquireThread(interpreter->threadPythonState);

	PyRun_String(bufferCondition,
			Py_file_input,
			interpreter->pdict,
			interpreter->pdict);

	interpreter->pval = PyDict_GetItemString(interpreter->pdict, "result");

	PyArg_Parse(interpreter->pval, "s", &cval);

	result = (cval != NULL && strcmp("True", cval) == 0);

	PyEval_ReleaseThread(interpreter->threadPythonState) ;

	return result;
}

void interpreterFree(Interpreter *interpreter) {

	Py_DECREF(interpreter->pdict);

	PyEval_AcquireThread(interpreter->threadPythonState);
	Py_EndInterpreter(interpreter->threadPythonState);
	PyEval_ReleaseLock();

}
