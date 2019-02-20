package config_parser

import (
	"config"
	"reflect"
	"testing"

	"github.com/golang/protobuf/proto"
)

var filterTests = []struct {
	before proto.Message
	after  proto.Message
}{
	{
		before: &config.MetricDefinition{
			MetricName: "A Metric!",
			MetaData: &config.MetricDefinition_Metadata{
				ExpirationDate: "A DATE!",
				Owner:          []string{"Someone"},
			},
		},
		after: &config.MetricDefinition{
			MetricName: "A Metric!",
			MetaData:   &config.MetricDefinition_Metadata{},
		},
	},

	{
		before: &config.ReportDefinition{
			ReportName:    "A Report!",
			CandidateList: []string{"Candidate1", "Candidate2", "Candidate3"},
		},
		after: &config.ReportDefinition{
			ReportName: "A Report!",
		},
	},
}

func TestFilter(t *testing.T) {
	for _, tt := range filterTests {
		if proto.Equal(tt.before, tt.after) {
			t.Errorf("%v\n==\n%v", proto.MarshalTextString(tt.before), proto.MarshalTextString(tt.after))
		}

		filterReflectedValue(reflect.ValueOf(tt.before))

		if !proto.Equal(tt.before, tt.after) {
			t.Errorf("%v\n!=\n%v", proto.MarshalTextString(tt.before), proto.MarshalTextString(tt.after))
		}
	}
}
