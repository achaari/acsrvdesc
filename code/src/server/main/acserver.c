e_srvrunstat_ acsrv_run_server(p_srvhndl_ srvhndlp, int argc, const char **argv)
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

	/* Check Serve Config */
	if (! acsrv_check_config(srvhndlp)) {
		return SRV_RUNSTAT_INVALID_CONFIG;
	}
	
	/* Register server */
	if (! acsrv_register_server(srvhndlp)) {
		return SRV_RUNSTAT_REG_FAILED;
	}
  
  	return SRV_RUNSTAT_OK; 
}
