// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
package package
const CustomerName string = "customer";
const ProjectName string = "project";

// Linear bucket constants for linear buckets
const LinearBucketsIntBucketsFloor int64 = 0;
const LinearBucketsIntBucketsNumBuckets uint32 = 140;
const LinearBucketsIntBucketsStepSize uint32 = 5;

// Exponential bucket constants for exponential buckets report
const ExponentialBucketsReportIntBucketsFloor int64 = 0;
const ExponentialBucketsReportIntBucketsNumBuckets uint32 = 3;
const ExponentialBucketsReportIntBucketsInitialStep uint32 = 2;
const ExponentialBucketsReportIntBucketsStepMultiplier uint32 = 2;

// Metric ID Constants
// the_metric_name
const TheMetricNameMetricId uint32 = 100;
// the_other_metric_name
const TheOtherMetricNameMetricId uint32 = 200;
// event groups
const EventGroupsMetricId uint32 = 300;
// linear buckets
const LinearBucketsMetricId uint32 = 400;
// exponential buckets
const ExponentialBucketsMetricId uint32 = 500;
// metric
const MetricMetricId uint32 = 600;
// second metric
const SecondMetricMetricId uint32 = 601;

// Enum for the_other_metric_name (Metric Dimension 0)
type TheOtherMetricNameMetricDimension0 uint32
const (
_ TheOtherMetricNameMetricDimension0 = iota
  AnEvent = 0
  AnotherEvent = 1
  AThirdEvent = 2
)

// Alias for event groups (Metric Dimension The First Group) which has the same event codes
type EventGroupsMetricDimensionTheFirstGroup = TheOtherMetricNameMetricDimension0

// Enum for event groups (Metric Dimension A second group)
type EventGroupsMetricDimensionASecondGroup uint32
const (
_ EventGroupsMetricDimensionASecondGroup = iota
  This = 1
  Is = 2
  Another = 3
  Test = 4
)

// Enum for event groups (Metric Dimension 2)
type EventGroupsMetricDimension2 uint32
const (
_ EventGroupsMetricDimension2 = iota
  ThisMetric = 0
  HasNo = 2
  Name = 4
)

// Enum for metric (Metric Dimension First)
type MetricMetricDimensionFirst uint32
const (
_ MetricMetricDimensionFirst = iota
  A = 1
  Set = 2
  OfEvent = 3
  Codes = 4
)

// Alias for second metric (Metric Dimension First) which has the same event codes
type SecondMetricMetricDimensionFirst = MetricMetricDimensionFirst

// Enum for metric (Metric Dimension Second)
type MetricMetricDimensionSecond uint32
const (
_ MetricMetricDimensionSecond = iota
  Some = 0
  More = 4
  Event = 8
  Codes = 16
)

// Alias for second metric (Metric Dimension Second) which has the same event codes
type SecondMetricMetricDimensionSecond = MetricMetricDimensionSecond

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
const Config string = "KoMGCghjdXN0b21lchAKGvQFCgdwcm9qZWN0EAUaQgoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhEKCnRoZV9yZXBvcnQQChiPTmIWChB0aGVfb3RoZXJfcmVwb3J0EBQYBhpsChV0aGVfb3RoZXJfbWV0cmljX25hbWUQChgFIMgBKAFQAWIQCgp0aGVfcmVwb3J0EAoYB4IBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGucBCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIQCgp0aGVfcmVwb3J0EB4YB4IBRQoPVGhlIEZpcnN0IEdyb3VwEgsIABIHQW5FdmVudBIQCAESDEFub3RoZXJFdmVudBIRCAISDUEgdGhpcmQgZXZlbnQYAoIBOQoOQSBzZWNvbmQgZ3JvdXASCAgBEgRUaGlzEgYIAhICSXMSCwgDEgdhbm90aGVyEggIBBIEVGVzdIIBNRIOCAASClRoaXNNZXRyaWMSCQgCEgVIYXNObxIICAQSBE5hbWUqDgoFSGFzTm8SBUFsaWFzGiAKDmxpbmVhciBidWNrZXRzEAoYBSCQA0IHEgUQjAEYBRoyChNleHBvbmVudGlhbCBidWNrZXRzEAoYBSD0A2IUCgZyZXBvcnQQKFIICgYQAxgCIAIadgoGbWV0cmljEAoYBSDYBIIBLwoFRmlyc3QSBQgBEgFBEgcIAhIDU2V0EgsIAxIHT2ZFdmVudBIJCAQSBUNvZGVzggEyCgZTZWNvbmQSCAgAEgRTb21lEggIBBIETW9yZRIJCAgSBUV2ZW50EgkIEBIFQ29kZXMafQoNc2Vjb25kIG1ldHJpYxAKGAUg2QSCAS8KBUZpcnN0EgUIARIBQRIHCAISA1NldBILCAMSB09mRXZlbnQSCQgEEgVDb2Rlc4IBMgoGU2Vjb25kEggIABIEU29tZRIICAQSBE1vcmUSCQgIEgVFdmVudBIJCBASBUNvZGVz";
