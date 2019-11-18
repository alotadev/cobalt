// This file was generated by Cobalt's Config parser based on the configuration
// YAML in the cobalt_config repository. Edit the YAML there to make changes.
const String customerName = 'customer';
// ignore: constant_identifier_names
const int customerId = 10;
const String projectName = 'project';
// ignore: constant_identifier_names
const int projectId = 5;

// Linear bucket constants for linear buckets
// ignore: constant_identifier_names
const int linearBucketsIntBucketsFloor = 0;
// ignore: constant_identifier_names
const int linearBucketsIntBucketsNumBuckets = 140;
// ignore: constant_identifier_names
const int linearBucketsIntBucketsStepSize = 5;

// Exponential bucket constants for exponential buckets report
// ignore: constant_identifier_names
const int exponentialBucketsReportIntBucketsFloor = 0;
// ignore: constant_identifier_names
const int exponentialBucketsReportIntBucketsNumBuckets = 3;
// ignore: constant_identifier_names
const int exponentialBucketsReportIntBucketsInitialStep = 2;
// ignore: constant_identifier_names
const int exponentialBucketsReportIntBucketsStepMultiplier = 2;

// Metric ID Constants
// the_metric_name
// ignore: constant_identifier_names
const int theMetricNameMetricId = 100;
// the_other_metric_name
// ignore: constant_identifier_names
const int theOtherMetricNameMetricId = 200;
// event groups
// ignore: constant_identifier_names
const int eventGroupsMetricId = 300;
// linear buckets
// ignore: constant_identifier_names
const int linearBucketsMetricId = 400;
// exponential buckets
// ignore: constant_identifier_names
const int exponentialBucketsMetricId = 500;
// metric
// ignore: constant_identifier_names
const int metricMetricId = 600;
// second metric
// ignore: constant_identifier_names
const int secondMetricMetricId = 601;

// Enum for the_other_metric_name (Metric Dimension 0)
class TheOtherMetricNameMetricDimension0 {
  static const int AnEvent = 0;
  static const int AnotherEvent = 1;
  static const int AThirdEvent = 2;
}
const int TheOtherMetricNameMetricDimension0_AnEvent = TheOtherMetricNameMetricDimension0::AnEvent;
const int TheOtherMetricNameMetricDimension0_AnotherEvent = TheOtherMetricNameMetricDimension0::AnotherEvent;
const int TheOtherMetricNameMetricDimension0_AThirdEvent = TheOtherMetricNameMetricDimension0::AThirdEvent;

// Enum for event groups (Metric Dimension The First Group)
class EventGroupsMetricDimensionTheFirstGroup {
  static const int AnEvent = 0;
  static const int AnotherEvent = 1;
  static const int AThirdEvent = 2;
}
const int EventGroupsMetricDimensionTheFirstGroup_AnEvent = EventGroupsMetricDimensionTheFirstGroup::AnEvent;
const int EventGroupsMetricDimensionTheFirstGroup_AnotherEvent = EventGroupsMetricDimensionTheFirstGroup::AnotherEvent;
const int EventGroupsMetricDimensionTheFirstGroup_AThirdEvent = EventGroupsMetricDimensionTheFirstGroup::AThirdEvent;

// Enum for event groups (Metric Dimension A second group)
class EventGroupsMetricDimensionASecondGroup {
  static const int This = 1;
  static const int Is = 2;
  static const int Another = 3;
  static const int Test = 4;
}
const int EventGroupsMetricDimensionASecondGroup_This = EventGroupsMetricDimensionASecondGroup::This;
const int EventGroupsMetricDimensionASecondGroup_Is = EventGroupsMetricDimensionASecondGroup::Is;
const int EventGroupsMetricDimensionASecondGroup_Another = EventGroupsMetricDimensionASecondGroup::Another;
const int EventGroupsMetricDimensionASecondGroup_Test = EventGroupsMetricDimensionASecondGroup::Test;

// Enum for event groups (Metric Dimension 2)
class EventGroupsMetricDimension2 {
  static const int ThisMetric = 0;
  static const int HasNo = 2;
  static const int Name = 4;
  static const int Alias = HasNo;
}
const int EventGroupsMetricDimension2_ThisMetric = EventGroupsMetricDimension2::ThisMetric;
const int EventGroupsMetricDimension2_HasNo = EventGroupsMetricDimension2::HasNo;
const int EventGroupsMetricDimension2_Name = EventGroupsMetricDimension2::Name;
const int EventGroupsMetricDimension2_Alias = EventGroupsMetricDimension2::Alias;

// Enum for metric (Metric Dimension First)
class MetricMetricDimensionFirst {
  static const int A = 1;
  static const int Set = 2;
  static const int OfEvent = 3;
  static const int Codes = 4;
}
const int MetricMetricDimensionFirst_A = MetricMetricDimensionFirst::A;
const int MetricMetricDimensionFirst_Set = MetricMetricDimensionFirst::Set;
const int MetricMetricDimensionFirst_OfEvent = MetricMetricDimensionFirst::OfEvent;
const int MetricMetricDimensionFirst_Codes = MetricMetricDimensionFirst::Codes;

// Enum for second metric (Metric Dimension First)
class SecondMetricMetricDimensionFirst {
  static const int A = 1;
  static const int Set = 2;
  static const int OfEvent = 3;
  static const int Codes = 4;
}
const int SecondMetricMetricDimensionFirst_A = SecondMetricMetricDimensionFirst::A;
const int SecondMetricMetricDimensionFirst_Set = SecondMetricMetricDimensionFirst::Set;
const int SecondMetricMetricDimensionFirst_OfEvent = SecondMetricMetricDimensionFirst::OfEvent;
const int SecondMetricMetricDimensionFirst_Codes = SecondMetricMetricDimensionFirst::Codes;

// Enum for metric (Metric Dimension Second)
class MetricMetricDimensionSecond {
  static const int Some = 0;
  static const int More = 4;
  static const int Event = 8;
  static const int Codes = 16;
}
const int MetricMetricDimensionSecond_Some = MetricMetricDimensionSecond::Some;
const int MetricMetricDimensionSecond_More = MetricMetricDimensionSecond::More;
const int MetricMetricDimensionSecond_Event = MetricMetricDimensionSecond::Event;
const int MetricMetricDimensionSecond_Codes = MetricMetricDimensionSecond::Codes;

// Enum for second metric (Metric Dimension Second)
class SecondMetricMetricDimensionSecond {
  static const int Some = 0;
  static const int More = 4;
  static const int Event = 8;
  static const int Codes = 16;
}
const int SecondMetricMetricDimensionSecond_Some = SecondMetricMetricDimensionSecond::Some;
const int SecondMetricMetricDimensionSecond_More = SecondMetricMetricDimensionSecond::More;
const int SecondMetricMetricDimensionSecond_Event = SecondMetricMetricDimensionSecond::Event;
const int SecondMetricMetricDimensionSecond_Codes = SecondMetricMetricDimensionSecond::Codes;

// The base64 encoding of the bytes of a serialized CobaltRegistry proto message.
const String config = 'KoMGCghjdXN0b21lchAKGvQFCgdwcm9qZWN0EAUaQgoPdGhlX21ldHJpY19uYW1lEAoYBSBkYhEKCnRoZV9yZXBvcnQQChiPTmIWChB0aGVfb3RoZXJfcmVwb3J0EBQYBhpsChV0aGVfb3RoZXJfbWV0cmljX25hbWUQChgFIMgBKAFQAWIQCgp0aGVfcmVwb3J0EAoYB4IBNRILCAASB0FuRXZlbnQSEAgBEgxBbm90aGVyRXZlbnQSEQgCEg1BIHRoaXJkIGV2ZW50GMgBGucBCgxldmVudCBncm91cHMQChgFIKwCKAFQAWIQCgp0aGVfcmVwb3J0EB4YB4IBRQoPVGhlIEZpcnN0IEdyb3VwEgsIABIHQW5FdmVudBIQCAESDEFub3RoZXJFdmVudBIRCAISDUEgdGhpcmQgZXZlbnQYAoIBOQoOQSBzZWNvbmQgZ3JvdXASCAgBEgRUaGlzEgYIAhICSXMSCwgDEgdhbm90aGVyEggIBBIEVGVzdIIBNRIOCAASClRoaXNNZXRyaWMSCQgCEgVIYXNObxIICAQSBE5hbWUqDgoFSGFzTm8SBUFsaWFzGiAKDmxpbmVhciBidWNrZXRzEAoYBSCQA0IHEgUQjAEYBRoyChNleHBvbmVudGlhbCBidWNrZXRzEAoYBSD0A2IUCgZyZXBvcnQQKFIICgYQAxgCIAIadgoGbWV0cmljEAoYBSDYBIIBLwoFRmlyc3QSBQgBEgFBEgcIAhIDU2V0EgsIAxIHT2ZFdmVudBIJCAQSBUNvZGVzggEyCgZTZWNvbmQSCAgAEgRTb21lEggIBBIETW9yZRIJCAgSBUV2ZW50EgkIEBIFQ29kZXMafQoNc2Vjb25kIG1ldHJpYxAKGAUg2QSCAS8KBUZpcnN0EgUIARIBQRIHCAISA1NldBILCAMSB09mRXZlbnQSCQgEEgVDb2Rlc4IBMgoGU2Vjb25kEggIABIEU29tZRIICAQSBE1vcmUSCQgIEgVFdmVudBIJCBASBUNvZGVz';
