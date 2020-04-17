package main

import (
	"net/http"

	"github.com/spf13/pflag"
	"github.com/spf13/viper"
	"pixielabs.ai/pixielabs/src/vizier/services/certmgr/certmgrenv"
	certmgrpb "pixielabs.ai/pixielabs/src/vizier/services/certmgr/certmgrpb"
	"pixielabs.ai/pixielabs/src/vizier/services/certmgr/controller"

	log "github.com/sirupsen/logrus"
	"pixielabs.ai/pixielabs/src/shared/services"
	"pixielabs.ai/pixielabs/src/shared/services/healthz"
)

func init() {
	pflag.String("namespace", "pl", "The namespace of Vizier")
	pflag.String("cluster_id", "", "The Cluster ID to use for Pixie Cloud")
}

func main() {
	log.WithField("service", "certmgr-service").Info("Starting service")

	services.SetupService("certmgr-service", 50900)
	services.SetupSSLClientFlags()
	services.PostFlagSetupAndParse()
	services.CheckServiceFlags()
	services.CheckSSLClientFlags()
	services.SetupServiceLogging()

	flush := services.InitDefaultSentry(viper.GetString("cluster_id"))
	defer flush()

	mux := http.NewServeMux()
	healthz.RegisterDefaultChecks(mux)

	k8sAPI, err := controller.NewK8sAPI(viper.GetString("namespace"))
	if err != nil {
		log.WithError(err).Fatal("Failed to connect to K8S API")
	}

	env := certmgrenv.New()
	server := controller.NewServer(env, k8sAPI)

	s := services.NewPLServer(env, mux)
	certmgrpb.RegisterCertMgrServiceServer(s.GRPCServer(), server)
	s.Start()
	s.StopOnInterrupt()
}
