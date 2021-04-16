package bridge_test

import (
	"context"
	"fmt"
	"io"
	"net"
	"sync"
	"testing"
	"time"

	"github.com/gofrs/uuid"
	"github.com/gogo/protobuf/proto"
	"github.com/gogo/protobuf/types"
	"github.com/nats-io/nats.go"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"golang.org/x/sync/errgroup"
	"google.golang.org/grpc"
	"google.golang.org/grpc/test/bufconn"
	batchv1 "k8s.io/api/batch/v1"

	"px.dev/pixie/src/cloud/vzconn/vzconnpb"
	"px.dev/pixie/src/shared/cvmsgspb"
	"px.dev/pixie/src/shared/k8s/metadatapb"
	"px.dev/pixie/src/utils"
	"px.dev/pixie/src/utils/pbutils"
	"px.dev/pixie/src/utils/testingutils"
	"px.dev/pixie/src/vizier/services/cloud_connector/bridge"
	"px.dev/pixie/src/vizier/vizierpb"
)

const bufSize = 1024 * 1024

type FakeVZConnServer struct {
	quitCh chan bool
	msgQ   []*vzconnpb.V2CBridgeMessage
	wg     *sync.WaitGroup
	t      *testing.T
}

func marshalAndSend(srv vzconnpb.VZConnService_NATSBridgeServer, topic string, msg proto.Message) error {
	var respAsAny *types.Any
	var err error
	if respAsAny, err = pbutils.MarshalAny(msg); err != nil {
		return err
	}
	outMsg := &vzconnpb.C2VBridgeMessage{
		Topic: topic,
		Msg:   respAsAny,
	}
	return srv.Send(outMsg)
}

func handleMsg(srv vzconnpb.VZConnService_NATSBridgeServer, msg *vzconnpb.V2CBridgeMessage) error {
	if msg.Topic == "register" {
		return marshalAndSend(srv, "registerAck", &cvmsgspb.RegisterVizierAck{Status: cvmsgspb.ST_OK})
	}
	if msg.Topic == "randomtopic" {
		return nil
	}
	if msg.Topic == "randomtopicNeedsResponse" {
		var unmarshal = &cvmsgspb.VLogMessage{}
		err := pbutils.UnmarshalAny(msg.Msg, unmarshal)
		if err != nil {
			return err
		}
		return marshalAndSend(srv, "randomtopicNeedsResponseAck", unmarshal)
	}

	return fmt.Errorf("Got unknown topic %s", msg.Topic)
}

// NATSBridge is the endpoint that all viziers connect to.
func (fs *FakeVZConnServer) RegisterVizierDeployment(ctx context.Context, req *vzconnpb.RegisterVizierDeploymentRequest) (*vzconnpb.RegisterVizierDeploymentResponse, error) {
	assert.Equal(fs.t, "084cb5f0-ff69-11e9-a63e-42010a8a0193", req.K8sClusterUID)
	assert.Equal(fs.t, "v1.14.10-gke.27", req.K8sClusterVersion)
	newID := uuid.Must(uuid.NewV4())
	return &vzconnpb.RegisterVizierDeploymentResponse{
		VizierID: utils.ProtoFromUUID(newID),
	}, nil
}

// NATSBridge is the endpoint that all viziers connect to.
func (fs *FakeVZConnServer) NATSBridge(srv vzconnpb.VZConnService_NATSBridgeServer) error {
	for {
		select {
		case <-srv.Context().Done():
			return nil
		case <-fs.quitCh:
			return nil
		default:
			msg, err := srv.Recv()
			if err != nil && err == io.EOF {
				// stream closed.
				return nil
			}
			if err != nil {
				return err
			}
			// Ignore heartbeats
			if msg.Topic != bridge.HeartbeatTopic {
				fs.msgQ = append(fs.msgQ, msg)
				err = handleMsg(srv, msg)
				if err != nil {
					fs.t.Errorf("Error marshalling: %+v", err)
					return err
				}
				fs.wg.Done()
			}
		}
	}
}

func newFakeVZConnServer(wg *sync.WaitGroup, t *testing.T) *FakeVZConnServer {
	return &FakeVZConnServer{
		quitCh: make(chan bool),
		wg:     wg,
		t:      t,
	}
}

type FakeVZChecker struct{}

func (f *FakeVZChecker) GetStatus() (time.Time, error) {
	return time.Now(), nil
}

type FakeVZInfo struct {
	externalAddr string
	port         int32
}

func makeFakeVZInfo(externalAddr string, port int32) bridge.VizierInfo {
	return &FakeVZInfo{
		externalAddr: externalAddr,
		port:         port,
	}
}

func (f *FakeVZInfo) GetAddress() (string, int32, error) {
	return f.externalAddr, f.port, nil
}

func (f *FakeVZInfo) GetVizierClusterInfo() (*cvmsgspb.VizierClusterInfo, error) {
	return &cvmsgspb.VizierClusterInfo{
		ClusterUID:     "084cb5f0-ff69-11e9-a63e-42010a8a0193",
		ClusterName:    "test-cluster",
		ClusterVersion: "v1.14.10-gke.27",
	}, nil
}

func (f *FakeVZInfo) GetK8sState() (map[string]*cvmsgspb.PodStatus, int32, int32, time.Time) {
	lastUpdatedTime := time.Unix(2, 0)
	podStatus := make(map[string]*cvmsgspb.PodStatus)
	podStatus["vizier-query-broker"] = &cvmsgspb.PodStatus{
		Name:   "vizier-query-broker",
		Status: metadatapb.RUNNING,
	}

	return podStatus, 3, 2, lastUpdatedTime
}

func (f *FakeVZInfo) LaunchJob(j *batchv1.Job) (*batchv1.Job, error) {
	return nil, nil
}

func (f *FakeVZInfo) ParseJobYAML(yamlStr string, imageTag map[string]string, envSubtitutions map[string]string) (*batchv1.Job, error) {
	return nil, nil
}

func (f *FakeVZInfo) CreateSecret(name string, literals map[string]string) error {
	return nil
}

func (f *FakeVZInfo) WaitForJobCompletion(name string) (bool, error) {
	return false, nil
}

func (f *FakeVZInfo) DeleteJob(name string) error {
	return nil
}

func (f *FakeVZInfo) GetJob(name string) (*batchv1.Job, error) {
	return nil, nil
}

func (f *FakeVZInfo) GetClusterUID() (string, error) {
	return "fake-uid", nil
}

func (f *FakeVZInfo) UpdateClusterID(string) error {
	return nil
}

func (f *FakeVZInfo) GetVizierPodLogs(string, bool, string) (string, error) {
	return "fake log", nil
}

func (f *FakeVZInfo) GetVizierPods() ([]*vizierpb.VizierPodStatus, []*vizierpb.VizierPodStatus, error) {
	fakeControlPlane := []*vizierpb.VizierPodStatus{
		&vizierpb.VizierPodStatus{
			Name: "A pod",
		},
	}
	fakeAgents := []*vizierpb.VizierPodStatus{
		&vizierpb.VizierPodStatus{
			Name: "Another pod",
		},
	}
	return fakeAgents, fakeControlPlane, nil
}

type testState struct {
	vzServer *FakeVZConnServer
	vzClient vzconnpb.VZConnServiceClient
	nats     *nats.Conn
	vzID     uuid.UUID
	jwt      string
	wg       *sync.WaitGroup
	lis      *bufconn.Listener
}

func createDialer(lis *bufconn.Listener) func(ctx context.Context, url string) (net.Conn, error) {
	return func(ctx context.Context, url string) (conn net.Conn, e error) {
		return lis.Dial()
	}
}

func makeTestState(t *testing.T) (*testState, func(t *testing.T)) {
	lis := bufconn.Listen(bufSize)
	s := grpc.NewServer()
	wg := &sync.WaitGroup{}
	vs := newFakeVZConnServer(wg, t)
	vzconnpb.RegisterVZConnServiceServer(s, vs)
	eg := errgroup.Group{}
	eg.Go(func() error { return s.Serve(lis) })

	ctx := context.Background()
	conn, err := grpc.DialContext(ctx, "bufnet", grpc.WithContextDialer(createDialer(lis)), grpc.WithInsecure())
	if err != nil {
		t.Fatalf("Got an error during GRPC setup: %+v", err)
	}
	vc := vzconnpb.NewVZConnServiceClient(conn)
	nc, natsCleanup := testingutils.MustStartTestNATS(t)

	cleanupFunc := func(t *testing.T) {
		s.GracefulStop()
		natsCleanup()
		conn.Close()

		err := eg.Wait()
		if err != nil {
			t.Fatalf("failed to start server: %v", err)
		}
	}

	u, err := uuid.FromString("31285cdd-1de9-4ab1-ae6a-0ba08c8c676c")
	if err != nil {
		t.Fatal("Could not parse UUID.")
	}

	return &testState{
		vzID:     u,
		vzServer: vs,
		vzClient: vc,
		nats:     nc,
		jwt:      testingutils.GenerateTestJWTToken(t, "jwt-key"),
		wg:       wg,
		lis:      lis,
	}, cleanupFunc
}

func TestNATSGRPCBridgeTest_CorrectRegistrationFlow(t *testing.T) {
	ts, cleanup := makeTestState(t)
	defer cleanup(t)

	ts.wg.Add(1)

	sessionID := time.Now().UnixNano()
	b := bridge.New(ts.vzID, ts.jwt, "", sessionID, ts.vzClient, makeFakeVZInfo("foobar", 123), ts.nats, &FakeVZChecker{})
	defer b.Stop()
	go b.RunStream()

	ts.wg.Wait()
	assert.Equal(t, 1, len(ts.vzServer.msgQ))

	register := ts.vzServer.msgQ[0]

	// Check the metadata
	assert.Equal(t, "register", register.Topic)
	assert.Equal(t, sessionID, register.SessionId)

	// Check the contents
	registerMsg := &cvmsgspb.RegisterVizierRequest{}
	err := pbutils.UnmarshalAny(register.Msg, registerMsg)
	if err != nil {
		t.Fatalf("Could not unmarshal: %+v", err)
	}
	assert.Equal(t, utils.ProtoToUUIDStr(registerMsg.VizierID), ts.vzID.String())
	assert.Equal(t, registerMsg.JwtKey, ts.jwt)
	assert.Equal(t, registerMsg.Address, "foobar")
	assert.Equal(t, "test-cluster", registerMsg.ClusterInfo.ClusterName)
	assert.Equal(t, "v1.14.10-gke.27", registerMsg.ClusterInfo.ClusterVersion)
	assert.Equal(t, "084cb5f0-ff69-11e9-a63e-42010a8a0193", registerMsg.ClusterInfo.ClusterUID)
}

// Test a message that comes from our NATS queue (and should end up sent to the VZConn)
func TestNATSGRPCBridgeTest_TestOutboundNATSMessage(t *testing.T) {
	ts, cleanup := makeTestState(t)
	defer cleanup(t)

	// wait for registration
	ts.wg.Add(1)

	sessionID := time.Now().UnixNano()
	b := bridge.New(ts.vzID, ts.jwt, "", sessionID, ts.vzClient, makeFakeVZInfo("foobar", 123), ts.nats, &FakeVZChecker{})
	defer func() {
		b.Stop()
	}()
	go b.RunStream()

	ts.wg.Wait()

	// log message
	ts.wg.Add(1)
	logmsg := &cvmsgspb.VLogMessage{
		Data: []byte("Foobar"),
	}
	subany, err := pbutils.MarshalAny(logmsg)
	if err != nil {
		t.Fatalf("Error marshalling msg: %+v", err)
	}
	v2cMsg := &cvmsgspb.V2CMessage{
		VizierID:  ts.vzID.String(),
		SessionId: sessionID,
		Msg:       subany,
	}
	serializedBytes, err := v2cMsg.Marshal()
	if err != nil {
		t.Fatalf("Error marshalling msg: %+v", err)
	}
	inMsg := &nats.Msg{Subject: "v2c.randomtopic", Data: serializedBytes}
	err = ts.nats.PublishMsg(inMsg)
	if err != nil {
		t.Fatalf("Error publishing NATS msg: %+v", err)
	}

	// wait for log message
	ts.wg.Wait()
	assert.Equal(t, 2, len(ts.vzServer.msgQ))

	msg := ts.vzServer.msgQ[1]
	assert.Equal(t, "randomtopic", msg.Topic)
	assert.Equal(t, sessionID, msg.SessionId)

	expected := &cvmsgspb.VLogMessage{}
	err = pbutils.UnmarshalAny(msg.Msg, expected)
	if err != nil {
		t.Fatalf("Error Unmarshaling: %+v", err)
	}

	assert.Equal(t, expected, logmsg)
}

// Test a message that is sent by VZConn and should end up in our NATS queue
func TestNATSGRPCBridgeTest_TestInboundNATSMessage(t *testing.T) {
	ts, cleanup := makeTestState(t)
	defer cleanup(t)

	// wait for registration
	ts.wg.Add(1)

	sessionID := time.Now().UnixNano()
	b := bridge.New(ts.vzID, ts.jwt, "", sessionID, ts.vzClient, makeFakeVZInfo("foobar", 123), ts.nats, &FakeVZChecker{})
	defer b.Stop()

	go b.RunStream()
	ts.wg.Wait()

	// Subscribe to NATS
	natsCh := make(chan *nats.Msg)
	natsSub, err := ts.nats.ChanSubscribe("c2v.*", natsCh)
	if err != nil {
		t.Fatalf("Error subscribing to channel: %+v", err)
	}

	var inboundNats *nats.Msg
	ts.wg.Add(1) // For the nats msg.
	go func() {
		inboundNats = <-natsCh
		err := natsSub.Unsubscribe()
		require.NoError(t, err)

		ts.wg.Done()
	}()

	// This message originates in the NATS queue but will trigger a response to also show up in the NATS queue.
	ts.wg.Add(1)
	logmsg := &cvmsgspb.VLogMessage{
		Data: []byte("Foobar"),
	}
	subany, err := pbutils.MarshalAny(logmsg)
	if err != nil {
		t.Fatalf("Error marshalling msg: %+v", err)
	}
	v2cMsg := &cvmsgspb.V2CMessage{
		VizierID:  ts.vzID.String(),
		SessionId: sessionID,
		Msg:       subany,
	}
	serializedBytes, err := v2cMsg.Marshal()
	if err != nil {
		t.Fatalf("Error marshalling msg: %+v", err)
	}
	inMsg := &nats.Msg{Subject: "v2c.randomtopicNeedsResponse", Data: serializedBytes}
	err = ts.nats.PublishMsg(inMsg)
	if err != nil {
		t.Fatalf("Error publishing NATS msg: %+v", err)
	}

	ts.wg.Wait()
	assert.Equal(t, 2, len(ts.vzServer.msgQ))
	assert.Equal(t, inboundNats.Subject, "c2v.randomtopicNeedsResponseAck")

	// Unmarshal and check the nats message
	expectedNats := &cvmsgspb.C2VMessage{
		VizierID: ts.vzID.String(),
		Msg:      subany,
	}

	actualNats := &cvmsgspb.C2VMessage{}
	err = actualNats.Unmarshal(inboundNats.Data)
	if err != nil {
		t.Fatalf("Error unmarshaling: %+v", err)
	}
	assert.Equal(t, expectedNats, actualNats)
}

func TestNATSGRPCBridgeTest_TestRegisterDeployment(t *testing.T) {
	ts, cleanup := makeTestState(t)
	defer cleanup(t)
	ts.wg.Add(1)

	vzID := uuid.FromStringOrNil("")

	sessionID := time.Now().UnixNano()
	b := bridge.New(vzID, ts.jwt, "", sessionID, ts.vzClient, makeFakeVZInfo("foobar", 123), ts.nats, &FakeVZChecker{})
	defer b.Stop()

	go b.RunStream()
	ts.wg.Wait()

	// Subscribe to NATS
	natsCh := make(chan *nats.Msg)
	natsSub, err := ts.nats.ChanSubscribe("c2v.*", natsCh)
	if err != nil {
		t.Fatalf("Error subscribing to channel: %+v", err)
	}
	go func() {
		<-natsCh
		err := natsSub.Unsubscribe()
		require.NoError(t, err)
		ts.wg.Done()
	}()
}
