// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
// Metric ID Constants
// the_metric_name
pub const THE_METRIC_NAME_METRIC_ID = 100;
// the_other_metric_name
pub const THE_OTHER_METRIC_NAME_METRIC_ID = 200;
// event groups
pub const EVENT_GROUPS_METRIC_ID = 300;

// Report ID Constants
// the_metric_name_the_other_report
pub const THE_METRIC_NAME_THE_OTHER_REPORT_REPORT_ID = 492006986;
// event groups_the_report
pub const EVENT_GROUPS_THE_REPORT_REPORT_ID = 2384646843;

// Enum for the_other_metric_name (EventCode)
pub enum TheOtherMetricNameEventCode {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Enum for the_other_metric_name (Metric Dimension 0)
pub enum TheOtherMetricNameMetricDimension0 {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Enum for event groups (Metric Dimension The First Group)
pub enum EventGroupsMetricDimensionTheFirstGroup {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Enum for event groups (Metric Dimension A second group)
pub enum EventGroupsMetricDimensionASecondGroup {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
}

// Enum for event groups (Metric Dimension 2)
pub enum EventGroupsMetricDimension2 {
  ThisMetric = 0,
  HasNo = 2,
  Name = 4,
}

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
pub const CONFIG: &str = "KrkDCghjdXN0b21lchAKGqoDCgdwcm9qZWN0EAUaSgoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGGnMKFXRoZV9vdGhlcl9tZXRyaWNfbmFtZRAKGAUgyAEoATjIAVABYhQKCnRoZV9yZXBvcnQQu6WL8QgYB4IBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGtsBCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAeCAUUKD1RoZSBGaXJzdCBHcm91cBILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GAKCATkKDkEgc2Vjb25kIGdyb3VwEggIARIEVGhpcxIGCAISAklzEgsIAxIHYW5vdGhlchIICAQSBFRlc3SCASUSDggAEgpUaGlzTWV0cmljEgkIAhIFSGFzTm8SCAgEEgROYW1l";
