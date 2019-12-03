// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
pub const CUSTOMER_NAME: &str = "the_customer";
pub const CUSTOMER_ID: u32 = 10;
pub const PROJECT_NAME: &str = "the_project";
pub const PROJECT_ID: u32 = 5;

// Linear bucket constants for linear buckets
pub const LINEAR_BUCKETS_INT_BUCKETS_FLOOR: i64 = 0;
pub const LINEAR_BUCKETS_INT_BUCKETS_NUM_BUCKETS: u32 = 140;
pub const LINEAR_BUCKETS_INT_BUCKETS_STEP_SIZE: u32 = 5;

// Exponential bucket constants for exponential buckets report
pub const EXPONENTIAL_BUCKETS_REPORT_INT_BUCKETS_FLOOR: i64 = 0;
pub const EXPONENTIAL_BUCKETS_REPORT_INT_BUCKETS_NUM_BUCKETS: u32 = 3;
pub const EXPONENTIAL_BUCKETS_REPORT_INT_BUCKETS_INITIAL_STEP: u32 = 2;
pub const EXPONENTIAL_BUCKETS_REPORT_INT_BUCKETS_STEP_MULTIPLIER: u32 = 2;

// Metric ID Constants
// the_metric_name
pub const THE_METRIC_NAME_METRIC_ID: u32 = 100;
// the_other_metric_name
pub const THE_OTHER_METRIC_NAME_METRIC_ID: u32 = 200;
// event groups
pub const EVENT_GROUPS_METRIC_ID: u32 = 300;
// linear buckets
pub const LINEAR_BUCKETS_METRIC_ID: u32 = 400;
// exponential buckets
pub const EXPONENTIAL_BUCKETS_METRIC_ID: u32 = 500;
// metric
pub const METRIC_METRIC_ID: u32 = 600;
// second metric
pub const SECOND_METRIC_METRIC_ID: u32 = 601;

// Enum for the_other_metric_name (Metric Dimension 0)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum TheOtherMetricNameMetricDimension0 {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Alias for event groups (Metric Dimension The First Group) which has the same event codes
pub use TheOtherMetricNameMetricDimension0 as EventGroupsMetricDimensionTheFirstGroup;

// Enum for the_project (Metric Dimension A second group)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum TheProjectMetricDimensionASecondGroup {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
}
// Alias for event groups (Metric Dimension A second group) which has the same event codes
pub use TheProjectMetricDimensionASecondGroup as EventGroupsMetricDimensionASecondGroup;

// Enum for the_project (Metric Dimension 2)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum TheProjectMetricDimension2 {
  ThisMetric = 0,
  HasNo = 2,
  Name = 4,
}
impl TheProjectMetricDimension2 {
  #[allow(non_upper_case_globals)]
  pub const Alias: TheProjectMetricDimension2 = TheProjectMetricDimension2::HasNo;
}
// Alias for event groups (Metric Dimension 2) which has the same event codes
pub use TheProjectMetricDimension2 as EventGroupsMetricDimension2;

// Enum for the_project (Metric Dimension First)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum TheProjectMetricDimensionFirst {
  A = 1,
  Set = 2,
  OfEvent = 3,
  Codes = 4,
}
// Alias for metric (Metric Dimension First) which has the same event codes
pub use TheProjectMetricDimensionFirst as MetricMetricDimensionFirst;

// Alias for second metric (Metric Dimension First) which has the same event codes
pub use TheProjectMetricDimensionFirst as SecondMetricMetricDimensionFirst;

// Enum for the_project (Metric Dimension Second)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum TheProjectMetricDimensionSecond {
  Some = 0,
  More = 4,
  Event = 8,
  Codes = 16,
}
// Alias for metric (Metric Dimension Second) which has the same event codes
pub use TheProjectMetricDimensionSecond as MetricMetricDimensionSecond;

// Alias for second metric (Metric Dimension Second) which has the same event codes
pub use TheProjectMetricDimensionSecond as SecondMetricMetricDimensionSecond;

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
pub const CONFIG: &str = "KssHCgx0aGVfY3VzdG9tZXIQChq4BwoLdGhlX3Byb2plY3QQBRpdCg90aGVfbWV0cmljX25hbWUQChgFIGRiEQoKdGhlX3JlcG9ydBAKGI9OYhYKEHRoZV9vdGhlcl9yZXBvcnQQFBgDcgx0aGVfY3VzdG9tZXJ6C3RoZV9wcm9qZWN0GocBChV0aGVfb3RoZXJfbWV0cmljX25hbWUQChgFIMgBKAFQAWIQCgp0aGVfcmVwb3J0EAoYB3IMdGhlX2N1c3RvbWVyegt0aGVfcHJvamVjdIIBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGoICCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIQCgp0aGVfcmVwb3J0EB4YB3IMdGhlX2N1c3RvbWVyegt0aGVfcHJvamVjdIIBRQoPVGhlIEZpcnN0IEdyb3VwEgsIABIHQW5FdmVudBIQCAESDEFub3RoZXJFdmVudBIRCAISDUEgdGhpcmQgZXZlbnQYAoIBOQoOQSBzZWNvbmQgZ3JvdXASCAgBEgRUaGlzEgYIAhICSXMSCwgDEgdhbm90aGVyEggIBBIEVGVzdIIBNRIOCAASClRoaXNNZXRyaWMSCQgCEgVIYXNObxIICAQSBE5hbWUqDgoFSGFzTm8SBUFsaWFzGjsKDmxpbmVhciBidWNrZXRzEAoYBSCQA0IHEgUQjAEYBXIMdGhlX2N1c3RvbWVyegt0aGVfcHJvamVjdBpNChNleHBvbmVudGlhbCBidWNrZXRzEAoYBSD0A2IUCgZyZXBvcnQQKFIICgYQAxgCIAJyDHRoZV9jdXN0b21lcnoLdGhlX3Byb2plY3QakQEKBm1ldHJpYxAKGAUg2ARyDHRoZV9jdXN0b21lcnoLdGhlX3Byb2plY3SCAS8KBUZpcnN0EgUIARIBQRIHCAISA1NldBILCAMSB09mRXZlbnQSCQgEEgVDb2Rlc4IBMgoGU2Vjb25kEggIABIEU29tZRIICAQSBE1vcmUSCQgIEgVFdmVudBIJCBASBUNvZGVzGpgBCg1zZWNvbmQgbWV0cmljEAoYBSDZBHIMdGhlX2N1c3RvbWVyegt0aGVfcHJvamVjdIIBLwoFRmlyc3QSBQgBEgFBEgcIAhIDU2V0EgsIAxIHT2ZFdmVudBIJCAQSBUNvZGVzggEyCgZTZWNvbmQSCAgAEgRTb21lEggIBBIETW9yZRIJCAgSBUV2ZW50EgkIEBIFQ29kZXM=";
