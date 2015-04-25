/*
 * interpreter.h
 *
 *  Created on: Apr 24, 2015
 *      Author: max
 */

#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stdlib.h>
#include <memory.h>
#include <Python.h>
#include <assert.h>

typedef struct _Interpreter {

	PyThreadState *threadPythonState;
	PyObject *pdict, *pval;

} Interpreter ;

void interpreterGlobalLoad();
void interpreterGlobalUnload();
Interpreter *interpreterCreate();
void interpreterAddVariable(Interpreter *interpreter, char *name, char *value);
int interpreterGetConditionValue(Interpreter *interpreter, char *condition);
void interpreterFree(Interpreter *interpreter);

#endif
