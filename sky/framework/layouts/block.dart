// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
library layouts.block;

import 'dart:sky' as sky;
import '../fn.dart';

class BlockLayout extends LayoutContainer {

  BlockLayout({
    Object key,
    List<UINode> children,
    Style style,
    String inlineStyle
  }) : super(
    key: key,
    children: children,
    style: style,
    inlineStyle: inlineStyle
  );

  void layout(sky.Element skyNode) {
    double y = 0.0;
    skyNode.width = skyNode.parentNode.width;
    skyNode.getChildNodes().forEach((child) {
      child.layout();
      child.x = 0.0;
      child.y = y;
      y += child.height;
    });
    skyNode.height = y;
  }
}
