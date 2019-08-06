// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
pub const CUSTOMER_NAME: &str = "customer";
pub const PROJECT_NAME: &str = "project";
// Metric ID Constants
// the_metric_name
pub const THE_METRIC_NAME_METRIC_ID: u32 = 100;
// the_other_metric_name
pub const THE_OTHER_METRIC_NAME_METRIC_ID: u32 = 200;
// event groups
pub const EVENT_GROUPS_METRIC_ID: u32 = 300;

// Enum for the_other_metric_name (Metric Dimension 0)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum TheOtherMetricNameMetricDimension0 {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Enum for event groups (Metric Dimension The First Group)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum EventGroupsMetricDimensionTheFirstGroup {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
}

// Enum for event groups (Metric Dimension A second group)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum EventGroupsMetricDimensionASecondGroup {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
}

// Enum for event groups (Metric Dimension 2)
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord, Debug, Hash)]
pub enum EventGroupsMetricDimension2 {
  ThisMetric = 0,
  HasNo = 2,
  Name = 4,
}
impl EventGroupsMetricDimension2 {
  #[allow(non_upper_case_globals)]
  pub const Alias: EventGroupsMetricDimension2 = EventGroupsMetricDimension2::HasNo;
}

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
pub const CONFIG: &str = "KsYDCghjdXN0b21lchAKGrcDCgdwcm9qZWN0EAUaSgoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGGnAKFXRoZV9vdGhlcl9tZXRyaWNfbmFtZRAKGAUgyAEoAVABYhQKCnRoZV9yZXBvcnQQu6WL8QgYB4IBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGusBCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAeCAUUKD1RoZSBGaXJzdCBHcm91cBILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GAKCATkKDkEgc2Vjb25kIGdyb3VwEggIARIEVGhpcxIGCAISAklzEgsIAxIHYW5vdGhlchIICAQSBFRlc3SCATUSDggAEgpUaGlzTWV0cmljEgkIAhIFSGFzTm8SCAgEEgROYW1lKg4KBUhhc05vEgVBbGlhcw==";