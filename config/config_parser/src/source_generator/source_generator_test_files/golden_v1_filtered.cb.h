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

// Enum for the_other_metric_name (EventCode)
enum class TheOtherMetricNameEventCode {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
const TheOtherMetricNameEventCode TheOtherMetricNameEventCode_AnEvent = TheOtherMetricNameEventCode::AnEvent;
const TheOtherMetricNameEventCode TheOtherMetricNameEventCode_AnotherEvent = TheOtherMetricNameEventCode::AnotherEvent;
const TheOtherMetricNameEventCode TheOtherMetricNameEventCode_AThirdEvent = TheOtherMetricNameEventCode::AThirdEvent;

// Enum for event groups (MetricDimensionThe First Group)
enum class EventGroupsMetricDimensionTheFirstGroup {
  AnEvent = 0,
  AnotherEvent = 1,
  AThirdEvent = 2,
};
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AnEvent = EventGroupsMetricDimensionTheFirstGroup::AnEvent;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AnotherEvent = EventGroupsMetricDimensionTheFirstGroup::AnotherEvent;
const EventGroupsMetricDimensionTheFirstGroup EventGroupsMetricDimensionTheFirstGroup_AThirdEvent = EventGroupsMetricDimensionTheFirstGroup::AThirdEvent;

// Enum for event groups (MetricDimensionA second group)
enum class EventGroupsMetricDimensionASecondGroup {
  This = 1,
  Is = 2,
  Another = 3,
  Test = 4,
};
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_This = EventGroupsMetricDimensionASecondGroup::This;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Is = EventGroupsMetricDimensionASecondGroup::Is;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Another = EventGroupsMetricDimensionASecondGroup::Another;
const EventGroupsMetricDimensionASecondGroup EventGroupsMetricDimensionASecondGroup_Test = EventGroupsMetricDimensionASecondGroup::Test;

// The base64 encoding of the bytes of a serialized CobaltConfig proto message.
const char kConfig[] = "Kv0BCghjdXN0b21lchAKGu4BCgdwcm9qZWN0EAUaSgoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGGjsKFXRoZV9vdGhlcl9tZXRyaWNfbmFtZRAKGAUgyAEoATjIAVABYhQKCnRoZV9yZXBvcnQQu6WL8QgYBxpYCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAeCARMKD1RoZSBGaXJzdCBHcm91cBgCggEQCg5BIHNlY29uZCBncm91cA==";
