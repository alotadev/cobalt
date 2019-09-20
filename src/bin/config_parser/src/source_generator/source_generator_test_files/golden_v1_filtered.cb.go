// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
package package
const CustomerName string = "customer";
const ProjectName string = "project";
// Metric ID Constants
// the_metric_name
const TheMetricNameMetricId uint32 = 100;
// the_other_metric_name
const TheOtherMetricNameMetricId uint32 = 200;
// event groups
const EventGroupsMetricId uint32 = 300;

// Enum for the_other_metric_name (Metric Dimension 0)
type TheOtherMetricNameMetricDimension0 uint32
const (
_ TheOtherMetricNameMetricDimension0 = iota
  AnEvent = 0
  AnotherEvent = 1
  AThirdEvent = 2
)

// Enum for event groups (Metric Dimension The First Group)
type EventGroupsMetricDimensionTheFirstGroup uint32
const (
_ EventGroupsMetricDimensionTheFirstGroup = iota
  AnEvent = 0
  AnotherEvent = 1
  AThirdEvent = 2
)

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

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
const Config string = "KsYDCghjdXN0b21lchAKGrcDCgdwcm9qZWN0EAUaSgoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhUKCnRoZV9yZXBvcnQQu6WL8QgYj05iGgoQdGhlX290aGVyX3JlcG9ydBDK3M3qARgGGnAKFXRoZV9vdGhlcl9tZXRyaWNfbmFtZRAKGAUgyAEoAVABYhQKCnRoZV9yZXBvcnQQu6WL8QgYB4IBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGusBCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIUCgp0aGVfcmVwb3J0ELuli/EIGAeCAUUKD1RoZSBGaXJzdCBHcm91cBILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GAKCATkKDkEgc2Vjb25kIGdyb3VwEggIARIEVGhpcxIGCAISAklzEgsIAxIHYW5vdGhlchIICAQSBFRlc3SCATUSDggAEgpUaGlzTWV0cmljEgkIAhIFSGFzTm8SCAgEEgROYW1lKg4KBUhhc05vEgVBbGlhcw==";
