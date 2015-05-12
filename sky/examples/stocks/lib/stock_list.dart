// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
library stocks.stock_list;

import 'package:sky/framework/components/fixed_height_scrollable.dart';
import 'package:sky/framework/fn.dart';
import 'stock_data.dart';
import 'stock_row.dart';

class Stocklist extends FixedHeightScrollable {
  String query;
  List<Stock> stocks;

  Stocklist({
    Object key,
    this.stocks,
    this.query
  }) : super(key: key);

  List<UINode> buildItems(int start, int count) {
    var filteredStocks = stocks.where((stock) {
      return query == null ||
             stock.symbol.contains(new RegExp(query, caseSensitive: false));
    });
    itemCount = filteredStocks.length;
    return filteredStocks
      .skip(start)
      .take(count)
      .map((stock) => new StockRow(stock: stock))
      .toList(growable: false);
  }
}
