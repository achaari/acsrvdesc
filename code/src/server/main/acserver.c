typedef struct srvhndl_ {
    p_ptrhnd_ srvdatap;
} ac_srvhndl_;

static e_srvrunstat_ acsrv_start_server(p_srvhndl_ srvhndlp)
{
    /* Allocate Server Main Session */
    if (! acsrv_allocate_session(srvhndlp, MAIN_SESSION, NULL)) {
	return SRV_RUNSTAT_START_PROCESS_THTEAD_FAILED;
    }

    /* Create Process thread */
    if (! acsrv_create_thread(srvhndlp, NULL, ACSRV_PROCESS_THREAD, NULL)) {
	return SRV_RUNSTAT_START_PROCESS_THTEAD_FAILED;
    }

    /* Create Management thread */
    if (! acsrv_create_thread(srvhndlp, NULL, ACSRV_MANAGEMENT_THREAD, NULL)) {
	return SRV_RUNSTAT_START_MANAGEMENT_THTEAD_FAILED;
    }
	
    /* Create Listening thread for non-secure connection */
    if (rvhndp->configp->nonsecurectxb && ! acsrv_create_thread(srvhndlp, NULL, ACSRV_LISTENING_THREAD, NULL)) {
	return SRV_RUNSTAT_START_LISTENING_THREAD_FAILED;
    }
	
    /* Create Listening thread for secure connection */
    if (rvhndp->configp->securectxb && ! acsrv_create_thread(srvhndlp, NULL, ACSRV_SECURE_LISTENING_THREAD, NULL)) {
	return SRV_RUNSTAT_START_LISTENING_THREAD_FAILED;
    }
	
    /* Run the server Main Loop */
    acsrv_runinig_loop(srvhndlp);

    /* Terminate Process loop */
    return acsrv_terminate_loop(srvhndlp);
}

static e_srvrunstat_ acsrv_run_server_impl(p_srvhndl_ srvhndlp, int argc, const char **argv)
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

static ac_bool_ acsrv_create_thread(p_srvhndl_ srvhndlp, p_ptrhnd_ threadownerp, eac_srvthread_type_ threadtype, p_ptrhnd_ threaddatap)
{
    acp_srvthread_ threadhndlp;

    if (threadtype == ACSRV_LISTENING_THREAD || threadtype == ACSRV_SECURE_LISTENING_THREAD) {
	/* Create Server Listening thread */
	threadhndlp = acsrv_alloc_listening_thread(srvhndlp, threadtype);
    }
    else {
	/* Create Server Processing thread */
	threadhndlp = acsrv_alloc_process_thread(srvhndlp, threadtype, threadownerp, threaddatap);
    }

    /* Register Server Thread */
    if (threadhndlp == NULL || !acsrv_register_thread(srvhndlp, threadhndlp)) {
	return FALSE;
    }

    /* Start Server Thread */
    return acsrv_start_thread(srvhndlp, threadhndlp);
}

static eac_thread_exit_flag_ acsrv_listening_thread(p_srvhndl_ srvhndlp, acp_srvthread_ threadhndlp)
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
