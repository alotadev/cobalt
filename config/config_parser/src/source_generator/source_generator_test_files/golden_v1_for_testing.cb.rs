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

// Report ID Constants
// the_other_report
pub const THE_OTHER_REPORT_REPORT_ID: u32 = 492006986;
// the_report
pub const THE_REPORT_REPORT_ID: u32 = 2384646843;

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
pub const CONFIG: &str = "KoAECghjdXN0b21lchAKGvEDCgdwcm9qZWN0EAUaXQoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGcghjdXN0b21lcnoHcHJvamVjdBqDAQoVdGhlX290aGVyX21ldHJpY19uYW1lEAoYBSDIASgBUAFiFAoKdGhlX3JlcG9ydBC7pYvxCBgHcghjdXN0b21lcnoHcHJvamVjdIIBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGv4BCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAdyCGN1c3RvbWVyegdwcm9qZWN0ggFFCg9UaGUgRmlyc3QgR3JvdXASCwgAEgdBbkV2ZW50EhAIARIMQW5vdGhlckV2ZW50EhEIAhINQSB0aGlyZCBldmVudBgCggE5Cg5BIHNlY29uZCBncm91cBIICAESBFRoaXMSBggCEgJJcxILCAMSB2Fub3RoZXISCAgEEgRUZXN0ggE1Eg4IABIKVGhpc01ldHJpYxIJCAISBUhhc05vEggIBBIETmFtZSoOCgVIYXNObxIFQWxpYXM=";
