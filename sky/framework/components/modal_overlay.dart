// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
library components.modal_overlay;

import '../fn.dart';

class ModalOverlay extends Component {
  static final Style _style = new Style('''
    position: absolute;
    top: 0;
    left: 0;
    bottom: 0;
    right: 0;''');

  List<UINode> children;
  GestureEventListener onDismiss;

  ModalOverlay({ Object key, this.children, this.onDismiss }) : super(key: key);

  UINode build() {
    return new EventListenerNode(
      new Container(
        style: _style,
        children: children),
      onGestureTap: onDismiss);
  }
}
