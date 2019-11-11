typedef struct srvhndl_ {
    p_ptrhnd_ srvdatap;
} ac_srvhndl_;

typedef struct srvthread_ {
    p_srvhndl_	 srvhndp;
    p_threadhnd_ mngthreadhnp;
} ac_srvthread_;

static eac_srvrunstat_ acsrv_start_server(p_srvhndl_ srvhndlp)
{
    /* Allocate Server Main Session */
    if (! acsrv_allocate_session(srvhndlp, MAIN_SESSION, NULL)) {
	return SRV_RUNSTAT_START_PROCESS_THTEAD_FAILED;
    }

    /* Create Process thread */
    if (! acsrv_create_srvthread(srvhndlp, NULL, ACSRV_PROCESS_THREAD, NULL)) {
	return SRV_RUNSTAT_START_PROCESS_THTEAD_FAILED;
    }

    /* Create Management thread */
    if (! acsrv_create_srvthread(srvhndlp, NULL, ACSRV_MANAGEMENT_THREAD, NULL)) {
	return SRV_RUNSTAT_START_MANAGEMENT_THTEAD_FAILED;
    }
	
    /* Create Listening thread for non-secure connection */
    if (rvhndp->configp->nonsecurectxb && ! acsrv_create_srvthread(srvhndlp, NULL, ACSRV_LISTENING_THREAD, NULL)) {
	return SRV_RUNSTAT_START_LISTENING_THREAD_FAILED;
    }
	
    /* Create Listening thread for secure connection */
    if (rvhndp->configp->securectxb && ! acsrv_create_srvthread(srvhndlp, NULL, ACSRV_SECURE_LISTENING_THREAD, NULL)) {
	return SRV_RUNSTAT_START_LISTENING_THREAD_FAILED;
    }
	
    /* Run the server Main Loop */
    acsrv_runinig_loop(srvhndlp);

    /* Terminate Process loop */
    return acsrv_terminate_loop(srvhndlp);
}

static eac_srvrunstat_ acsrv_run_server_impl(p_srvhndl_ srvhndlp, int argc, const char **argv)
{
    /* Initialize server */
    if (srvhndlp->initcbkfctp != nullcbk) {
	if ((*srvhndlp->initcbkfctp)(srvhndlp, srvhndlp->datap, srvhndp->configp, argc, argv) != TRUE) {
	    return SRV_RUNSTAT_CBKINIT_FAILED;
	}
    }
    else if (! acsrv_init_server(srvhndp, argc, argv)) {
	return SRV_RUNSTAT_INIT_FAILED;
    }
	
    /* Check Server Config */
    if (! acsrv_check_config(srvhndlp)) {
	return SRV_RUNSTAT_INVALID_CONFIG;
    }
	
    /* Register server */
    if (! acsrv_register_server(srvhndlp)) {
	return SRV_RUNSTAT_REG_FAILED;
    }
	
    /* Start Server */
    return acsrv_start_server(srvhndlp); 
}

e_srvrunstat_ acsrv_run_server(p_srvhndl_ srvhndlp, int argc, const char **argv)
{
    e_srvrunstat_ runstat;
    
    while (TRUE) {
	runstat = acsrv_run_server_impl(srvhndlp, argc, argv);

	if (runstat == SRV_RUNSTAT_OK) {
	    break;
	}

	/* Terminate all Server pre-alloced process */
	acsrv_clean_server_proc(srvhndlp);

	if (! acsrv_wait_for_next_attempt(srvhndlp)) {
	    break;
	}
    }

    return runstat;
}

p_srvhndl_ acsrv_alloc_server(ac_bool_ allowsslb, size_t datasizel, CBKHND freecbkfp)
{
    p_srvhndl_ srvhndp = acuti_mem_alloc(sizeof(ac_srvhndl_));

    if (srvhndp == NULL) {
	return NULL;
    }

    if (datasizel > 0) {
	srvhndp->srvdatap = acuti_mem_alloc_ptrhnd(datasizel, freecbkfp);
	if (srvhndp->srvdatap == NULL) {
	    acsrv_free_server(&srvhndp);
	    return NULL;
	}
    }

    return srvhndp;
}

static ac_bool_ acsrv_run_srvthread(p_srvhndl_ srvhndlp, p_srvthread_ threadhndlp, acp_threadexe_ threadexep)
{
    while (acsrv_srvthread_process_next(srvhndlp, threadhndlp)) {

	/* Start new thread */
	if (! acthread_start_thread(srvhndlp, srvsrv_manage_srvthread_exe, TRUE, threadexep)) {
	    return FALSE;
	}
    }

    return TRUE;
}

static ac_bool_ acsrv_start_srvthread_workers(p_srvhndl_ srvhndlp, p_srvthread_ threadhndlp)
{
    int idx = 1, maxidx = 1;

    /* Get Max Server Thread Worker occurrences */
    maxidx = acsrv_getmax_occ(srvhndlp, threadhndlp->threadtyp);

    /* Start Server Thread Workers */
    for (; idx <= maxidx; idx++) {
	if (! acthread_start_thread(srvhndlp, srvsrv_manage_srvthread_worker, FALSE, threadhndlp)) {
	    return FALSE;
	}
    }

    /* Wait for Server Thread Workers termination */
    return acsrv_wait_for_srvthread_workers(srvhndlp, threadhndlp);
}

static ac_bool_ acsrv_start_srvthread(p_srvhndl_ srvhndlp, p_srvthread_ threadhndlp)
{
    /* Start Management thread for Server Threads in suspend */
    if (! acthread_startsuspend_thread(srvhndlp, srvsrv_manage_srvthread, threadhndlp)) {
	return FALSE;
    }

    /* Add to Server waining Thread list */
    if (! acsrv_add_to_waiting_thread(srvhndlp, threadhndlp)) {
	return FALSE;
    }

    if (acsrv_check_immediate_start(srvhndlp, threadhndlp)) {
	/* Resume Server Thread Manager */
	if (! acthread_resume_thread(srvhndlp, threadhndlp->mngthreadhnp)) {
	    return FALSE;
	}

	/* Set Current server thread in process */
	acsrv_set_srvthread_inprocess(srvhndlp, threadhndlp);
    }
    else if (! acsrv_add_to_delayed_start(srvhndlp, threadhndlp)) {
	return FALSE;
    }

    return TRUE;
}

static ac_bool_ acsrv_create_srvthread(p_srvhndl_ srvhndlp, p_ptrhnd_ threadownerp, eac_srvthread_type_ threadtype, p_ptrhnd_ threaddatap)
{
    p_srvthread_ threadhndlp;

    if (threadtype == ACSRV_LISTENING_THREAD || threadtype == ACSRV_SECURE_LISTENING_THREAD) {
	/* Create Server Listening thread */
	threadhndlp = acsrv_alloc_listening_srvthread(srvhndlp, threadtype);
    }
    else {
	/* Create Server Processing thread */
	threadhndlp = acsrv_alloc_process_srvthread(srvhndlp, threadtype, threadownerp, threaddatap);
    }

    if (threadhndlp == NULL) {
	return FALSE;
    } 
    
    /* Register Server Thread */
    if (! acsrv_register_srvthread(srvhndlp, threadhndlp)) {
	
	acsrv_terminate_srvthread(srvhndlp, threadhndlp, TRUE);
	return FALSE;
    }

    /* Start Server Thread  (immediate or delayed run) */
    if (! acsrv_start_srvthread(srvhndlp, threadhndlp)) {

	acsrv_terminate_srvthread(srvhndlp, threadhndlp, TRUE);
	return FALSE;
    }

    return TRUE;
}

static eac_thread_exit_flag_ acsrv_listening_srvthread(p_srvhndl_ srvhndlp, p_srvthread_ threadhndlp)
{
    while (acsrv_still_listening(srvhndlp, threadhndlp)) {

	if (acsrv_accept_connextion(srvhndlp, threadhndlp)) {

	    /* Create new Server Session */
	    if (! acsrv_create_new_session(srvhndlp, threadhndlp)) {

		/* Reply Server Error and close current connexion */
		acsrv_srv_error_reply(srvhndlp, acsrv_get_thread_connexion(threadhndlp), 
				      SRV_ERROR_FATAL, SRV_REPLY_ERROR_FAILED_SESSION);
	    }
	}
    }

    return threadhndlp->exitflagb;
}
