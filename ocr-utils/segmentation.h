// -*- C++ -*-

// Copyright 2006 Deutsches Forschungszentrum fuer Kuenstliche Intelligenz
// or its licensors, as applicable.
//
// You may not use this file except under the terms of the accompanying license.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you
// may not use this file except in compliance with the License. You may
// obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Project:
// File:
// Purpose:
// Responsible: tmb
// Reviewer:
// Primary Repository:
// Web Sites:

/// \file segmentation.h
/// \brief Some make_Xxx() functions for line segmenters

#ifndef h_segmentation_
#define h_segmentation_

#include "ocropus.h"

namespace ocropus {
    void count_neighbors(colib::bytearray &result,colib::bytearray &image);
    void find_endpoints(colib::bytearray &result,colib::bytearray &image);
    void find_junctions(colib::bytearray &result,colib::bytearray &image);
    void remove_singular_points(colib::bytearray &image,int d);
    colib::ISegmentLine *make_CurvedCutSegmenter();
    colib::ISegmentLine *make_ConnectedComponentSegmenter();
    colib::ISegmentLine *make_SkelSegmenter();
}

#endif
