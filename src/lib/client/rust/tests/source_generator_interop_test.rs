// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    cobalt_client::traits::{AsEventCode, AsEventCodes},
    rustc_registry::*,
};

#[test]
fn verify_generated_traits() {
    assert_eq!((ATestMetricMetricDimensionDim1::Unknown).as_event_code(), 1);
    assert_eq!(
        (
            ATestMetricMetricDimensionDim1::Known,
            ATestMetricMetricDimensionDim2::C
        )
            .as_event_codes(),
        vec![2, 3]
    );
}
