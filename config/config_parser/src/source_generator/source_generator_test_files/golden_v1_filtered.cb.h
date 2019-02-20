// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
#pragma once

// Metric ID Constants
// the_metric_name
const uint32_t kTheMetricNameMetricId = 100;
// the_other_metric_name
const uint32_t kTheOtherMetricNameMetricId = 200;
// event groups
const uint32_t kEventGroupsMetricId = 300;

// Report ID Constants
// the_metric_name_the_other_report
const uint32_t kTheMetricNameTheOtherReportReportId = 492006986;
// event groups_the_report
const uint32_t kEventGroupsTheReportReportId = 2384646843;

// Enum for the_other_metric_name (EventCode)
namespace the_other_metric_name_event_code_scope {
enum Enum {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
}  // the_other_metric_name_event_code_scope
typedef the_other_metric_name_event_code_scope::Enum TheOtherMetricNameEventCode;
const TheOtherMetricNameEventCode TheOtherMetricNameEventCode_AnEvent = TheOtherMetricNameEventCode::AnEvent;
const TheOtherMetricNameEventCode TheOtherMetricNameEventCode_AnotherEvent = TheOtherMetricNameEventCode::AnotherEvent;
const TheOtherMetricNameEventCode TheOtherMetricNameEventCode_AThirdEvent = TheOtherMetricNameEventCode::AThirdEvent;

// Enum for the_other_metric_name (Metric Dimension 0)
namespace the_other_metric_name_metric_dimension_0_scope {
enum Enum {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
}  // the_other_metric_name_metric_dimension_0_scope
typedef the_other_metric_name_metric_dimension_0_scope::Enum TheOtherMetricNameMetricDimension0;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AnEvent = TheOtherMetricNameMetricDimension0::AnEvent;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AnotherEvent = TheOtherMetricNameMetricDimension0::AnotherEvent;
const TheOtherMetricNameMetricDimension0 TheOtherMetricNameMetricDimension0_AThirdEvent = TheOtherMetricNameMetricDimension0::AThirdEvent;

// Enum for event groups (Metric Dimension The First Group)
namespace event_groups_metric_dimension_the_first_group_scope {
enum Enum {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
}  // event_groups_metric_dimension_the_first_group_scope
typedef event_groups_metric_dimension_the_first_group_scope::Enum EventGroupsMetricDimensionTheFirstGroup;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AnEvent = EventGroupsMetricDimensionTheFirstGroup::AnEvent;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AnotherEvent = EventGroupsMetricDimensionTheFirstGroup::AnotherEvent;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AThirdEvent = EventGroupsMetricDimensionTheFirstGroup::AThirdEvent;

// Enum for event groups (Metric Dimension A second group)
namespace event_groups_metric_dimension_a_second_group_scope {
enum Enum {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
};
}  // event_groups_metric_dimension_a_second_group_scope
typedef event_groups_metric_dimension_a_second_group_scope::Enum EventGroupsMetricDimensionASecondGroup;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_This = EventGroupsMetricDimensionASecondGroup::This;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Is = EventGroupsMetricDimensionASecondGroup::Is;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Another = EventGroupsMetricDimensionASecondGroup::Another;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Test = EventGroupsMetricDimensionASecondGroup::Test;

// Enum for event groups (Metric Dimension 2)
namespace event_groups_metric_dimension_2_scope {
enum Enum {
  ThisMetric = 0,
  HasNo = 2,
  Name = 4,
};
}  // event_groups_metric_dimension_2_scope
typedef event_groups_metric_dimension_2_scope::Enum EventGroupsMetricDimension2;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_ThisMetric = EventGroupsMetricDimension2::ThisMetric;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_HasNo = EventGroupsMetricDimension2::HasNo;
const EventGroupsMetricDimension2 EventGroupsMetricDimension2_Name = EventGroupsMetricDimension2::Name;

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
const char kConfig[] = "KrkDCghjdXN0b21lchAKGqoDCgdwcm9qZWN0EAUaSgoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGGnMKFXRoZV9vdGhlcl9tZXRyaWNfbmFtZRAKGAUgyAEoATjIAVABYhQKCnRoZV9yZXBvcnQQu6WL8QgYB4IBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGtsBCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAeCAUUKD1RoZSBGaXJzdCBHcm91cBILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GAKCATkKDkEgc2Vjb25kIGdyb3VwEggIARIEVGhpcxIGCAISAklzEgsIAxIHYW5vdGhlchIICAQSBFRlc3SCASUSDggAEgpUaGlzTWV0cmljEgkIAhIFSGFzTm8SCAgEEgROYW1l";
