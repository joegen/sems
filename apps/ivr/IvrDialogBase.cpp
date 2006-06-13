#include "IvrDialogBase.h"
#include "IvrAudio.h"
#include "Ivr.h"

#include "IvrSipDialog.h"

//#include "AmSessionTimer.h"

// Data definition
typedef struct {
    
    PyObject_HEAD
    PyObject* dialog;
    IvrDialog* p_dlg;
    
} IvrDialogBase;


// Constructor
static PyObject* IvrDialogBase_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"ivr_dlg", NULL};
    IvrDialogBase *self;

    self = (IvrDialogBase *)type->tp_alloc(type, 0);
    if (self != NULL) {
	
    	PyObject* o_dlg = NULL;
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist, &o_dlg)){
	    
	    Py_DECREF(self);
	    return NULL;
	}
    
	if (!PyCObject_Check(o_dlg)){
	    
	    Py_DECREF(self);
	    return NULL;
	}
	
	self->p_dlg = (IvrDialog*)PyCObject_AsVoidPtr(o_dlg);
	
	// initialize self.dialog
	self->dialog = IvrSipDialog_FromPtr(&self->p_dlg->dlg);

	if(!self->dialog){
	    PyErr_Print();
	    ERROR("IvrDialogBase: while creating IvrSipDialog instance\n");
	    Py_DECREF(self);
	    return NULL;
	}
    }

    DBG("IvrDialogBase_new\n");
    return (PyObject *)self;
}

static void
IvrDialogBase_dealloc(IvrDialogBase* self) 
{
  DBG("IvrDialogBase_dealloc\n");
  Py_XDECREF(self->dialog);
  self->ob_type->tp_free((PyObject*)self);
}

//
// Event handlers
//
// static PyObject* IvrDialogBase_onSessionStart(IvrDialogBase* self, PyObject*)
// {
//     DBG("no script implementation for onSessionStart(self,hdrs) !!!\n");

//     PyErr_SetNone(PyExc_NotImplementedError);
//     return NULL; // no return value
// }

// static PyObject* IvrDialogBase_onBye(IvrDialogBase* self, PyObject*)
// {
//     DBG("no script implementation for onBye(self) !!!\n");

//     PyErr_SetNone(PyExc_NotImplementedError);
//     return NULL; // no return value
// }

// static PyObject* IvrDialogBase_onEmptyQueue(IvrDialogBase* self, PyObject*)
// {
//     DBG("no script implementation for onEmptyQueue(self) !!!\n");

//     PyErr_SetNone(PyExc_NotImplementedError);
//     return NULL; // no return value
// }

// static PyObject* IvrDialogBase_onDtmf(IvrDialogBase* self, PyObject* args)
// {
//     int key, duration;
//     if(!PyArg_ParseTuple(args,"ii",&key,&duration))
// 	return NULL;

//     DBG("IvrDialogBase_onDtmf(%i,%i)\n",key,duration);

//     Py_INCREF(Py_None);
//     return Py_None;
// }

// static PyObject* IvrDialogBase_onTimer(IvrDialogBase* self, PyObject* args)
// {
//     DBG("IvrDialog::onTimer: no script implementation!!!\n");

//     PyErr_SetNone(PyExc_NotImplementedError);
//     return NULL; // no return value
// }

// static PyObject* IvrDialogBase_onOtherBye(IvrDialogBase* self, PyObject*)
// {
//     DBG("IvrDialogBase_onOtherBye()\n");

//     Py_INCREF(Py_None);
//     return Py_None;
// }

// static PyObject* IvrDialogBase_onOtherReply(IvrDialogBase* self, PyObject* args)
// {
//     DBG("IvrDialogBase_onOtherReply()\n");

//     int code;
//     char* reason;

//     if(!PyArg_ParseTuple(args,"is",&code,&reason))
// 	return NULL;
    

//     Py_INCREF(Py_None);
//     return Py_None;
// }

//
// Call control
//
static PyObject* IvrDialogBase_stopSession(IvrDialogBase* self, PyObject*)
{
    assert(self->p_dlg);
    self->p_dlg->setStopped();
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_bye(IvrDialogBase* self, PyObject*)
{
    assert(self->p_dlg);
    self->p_dlg->dlg.bye();
    Py_INCREF(Py_None);
    return Py_None;
}

//
// Media control
//
static PyObject* IvrDialogBase_enqueue(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    
    PyObject *o_play, *o_rec;
    AmAudioFile *a_play=NULL, *a_rec=NULL;
    
    if(!PyArg_ParseTuple(args,"OO",&o_play,&o_rec))
	return NULL;
    
    if (o_play != Py_None){
	
	if(!PyObject_TypeCheck(o_play,&IvrAudioFileType)){
	    
	    PyErr_SetString(PyExc_TypeError,"Argument 1 is no IvrAudioFile");
	    return NULL;
	}
	
	a_play = ((IvrAudioFile*)o_play)->af;
	a_play->rewind();
    }
    
    if (o_rec != Py_None){
	
	if(!PyObject_TypeCheck(o_rec,&IvrAudioFileType)){
	    
	    PyErr_SetString(PyExc_TypeError,"Argument 2 is no IvrAudioFile");
	    return NULL;
	}
	
	a_rec = ((IvrAudioFile*)o_rec)->af;
    }
    
    self->p_dlg->playlist.addToPlaylist(new AmPlaylistItem(a_play,a_rec));
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_flush(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);

    self->p_dlg->playlist.close();
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_mute(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);

    self->p_dlg->setMute(true);
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_unmute(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);

    self->p_dlg->setMute(false);
    
    Py_INCREF(Py_None);
    return Py_None;
}

// DTMF

static PyObject* IvrDialogBase_enableDTMFDetection(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);

    self->p_dlg->setDtmfDetectionEnabled(true);
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_disableDTMFDetection(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);

    self->p_dlg->setDtmfDetectionEnabled(false);
    
    Py_INCREF(Py_None);
    return Py_None;
}

// B2B methods
static PyObject* IvrDialogBase_b2b_connectCallee(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    
    string remote_party, remote_uri;

    PyObject* py_o;

    if((PyArg_ParseTuple(args,"O",&py_o)) && (py_o == Py_None)) {
      DBG("args == Py_None\n");
      remote_party = self->p_dlg->getOriginalRequest().to;
      remote_uri = self->p_dlg->getOriginalRequest().r_uri;
    } else {
      DBG("args != Py_None\n");
      char* rp = 0; char* ru = 0;
      if(!PyArg_ParseTuple(args,"ss",&rp, &ru))
	return NULL;
      else {
	remote_party = string(rp);
	remote_uri = string(ru);
      } 
    }
    
    self->p_dlg->connectCallee(remote_party, remote_uri);
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_b2b_set_relayonly(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    self->p_dlg->set_sip_relay_only(true);
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_b2b_set_norelayonly(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    self->p_dlg->set_sip_relay_only(false);
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_b2b_terminate_leg(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    self->p_dlg->terminateLeg();
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_b2b_terminate_other_leg(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    self->p_dlg->terminateOtherLeg();
    
    Py_INCREF(Py_None);
    return Py_None;
}


// Timer methods
static PyObject* IvrDialogBase_setTimer(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    
    int id = 0, interval = 0;
    if(!PyArg_ParseTuple(args,"ii",&id, &interval))
	return NULL;
    
    if (id <= 0) {
      ERROR("IVR script tried to set timer with non-positive ID.\n");
      return NULL;
    }

    AmArgArray di_args,ret;
    di_args.push(id);
    di_args.push(interval);
    di_args.push(self->p_dlg->getLocalTag().c_str());

    self->p_dlg->user_timer->
	invoke("setTimer", di_args, ret);
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* IvrDialogBase_removeTimer(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    
    int id = 0;
    if(!PyArg_ParseTuple(args,"i",&id))
	return NULL;
    
    if (id <= 0) {
      ERROR("IVR script tried to remove timer with non-positive ID.\n");
      return NULL;
    }

    AmArgArray di_args,ret;
    di_args.push(id);
    di_args.push(self->p_dlg->getLocalTag().c_str());

    self->p_dlg->user_timer->
      invoke("removeTimer",di_args,ret);
    
    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* IvrDialogBase_removeTimers(IvrDialogBase* self, PyObject* args)
{
    assert(self->p_dlg);
    
    AmArgArray di_args,ret;
    di_args.push(self->p_dlg->getLocalTag().c_str());

    self->p_dlg->user_timer->
      invoke("removeUserTimers",di_args,ret);
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject*
IvrDialogBase_getdialog(IvrDialogBase *self, void *closure)
{
  Py_INCREF(self->dialog);
  return self->dialog;
}


static PyMethodDef IvrDialogBase_methods[] = {
    
    // Event handlers
//     {"onSessionStart", (PyCFunction)IvrDialogBase_onSessionStart, METH_VARARGS,
//      "Gets called on session start"
//     },
//     {"onBye", (PyCFunction)IvrDialogBase_onBye, METH_NOARGS,
//      "Gets called if we received a BYE"
//     },
//     {"onEmptyQueue", (PyCFunction)IvrDialogBase_onEmptyQueue, METH_NOARGS,
//      "Gets called when the audio queue runs out of items"
//     },
//     {"onDtmf", (PyCFunction)IvrDialogBase_onDtmf, METH_VARARGS,
//      "Gets called when dtmf have been received"
//     },
//     {"onTimer", (PyCFunction)IvrDialogBase_onTimer, METH_VARARGS,
//      "Gets called when a timer is fired"
//     },
    
    // Call control
    {"stopSession", (PyCFunction)IvrDialogBase_stopSession, METH_NOARGS,
     "Stop the session"
    },
    {"bye", (PyCFunction)IvrDialogBase_bye, METH_NOARGS,
     "Send a BYE"
    },
    
    // Media control
    {"enqueue", (PyCFunction)IvrDialogBase_enqueue, METH_VARARGS,
     "Add some audio to the queue (mostly IvrAudioFile)"
    },
    {"flush", (PyCFunction)IvrDialogBase_flush, METH_NOARGS,
     "Flush the queue"
    },
    {"mute", (PyCFunction)IvrDialogBase_mute, METH_NOARGS,
     "mute the RTP stream (don't send packets)"
    },
    {"unmute", (PyCFunction)IvrDialogBase_unmute, METH_NOARGS,
     "unmute the RTP stream (do send packets)"
    },

    // DTMF
    {"enableDTMFDetection", (PyCFunction)IvrDialogBase_enableDTMFDetection, METH_NOARGS,
     "enable the dtmf detection"
    },
    {"disableDTMFDetection", (PyCFunction)IvrDialogBase_disableDTMFDetection, METH_NOARGS,
     "disable the dtmf detection"
    },    

    // B2B
    {"connectCallee", (PyCFunction)IvrDialogBase_b2b_connectCallee, METH_VARARGS,
     "call given party as (new) callee,"
     "if remote_party and remote_uri are empty (None),"
     "we will connect to the callee of the initial caller request"
    },
    {"terminateLeg", (PyCFunction)IvrDialogBase_b2b_terminate_leg, METH_VARARGS,
     "Terminate our leg and forget the other"
    },
    {"terminateOtherLeg", (PyCFunction)IvrDialogBase_b2b_terminate_other_leg, METH_VARARGS,
     "Terminate the other leg and forget it"
    },
    {"setRelayonly", (PyCFunction)IvrDialogBase_b2b_set_relayonly, METH_NOARGS,
     "sip requests will be relayed, and not processed"
    },
    {"setNoRelayonly", (PyCFunction)IvrDialogBase_b2b_set_norelayonly, METH_NOARGS,
     "sip requests will be processed"
    },
    // Timers
    {"setTimer", (PyCFunction)IvrDialogBase_setTimer, METH_VARARGS,
     "set a timer with id and t seconds timeout"
    },
    {"removeTimer", (PyCFunction)IvrDialogBase_removeTimer, METH_VARARGS,
     "remove a timer by id"
    },    
    {"removeTimers", (PyCFunction)IvrDialogBase_removeTimers, METH_NOARGS,
     "remove all timers"
    },    

    {NULL}  /* Sentinel */
};

static PyGetSetDef IvrDialogBase_getset[] = {
    {"dialog", 
     (getter)IvrDialogBase_getdialog, NULL,
     "the dialog class",
     NULL},
    {NULL}  /* Sentinel */
};

PyTypeObject IvrDialogBaseType = {
    
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "ivr.IvrDialogBase",       /*tp_name*/
    sizeof(IvrDialogBase),     /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)IvrDialogBase_dealloc,     /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "Base class for IvrDialog", /*tp_doc*/
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    IvrDialogBase_methods,     /* tp_methods */
    0,                         /* tp_members */
    IvrDialogBase_getset,      /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    IvrDialogBase_new,         /* tp_new */
};
