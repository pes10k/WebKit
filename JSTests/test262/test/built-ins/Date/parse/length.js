// Copyright (C) 2024 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-date.parse
description: >
  Date.parse.length is 1.
info: |
  Date.parse ( string )

  17 ECMAScript Standard Built-in Objects:
    Every built-in function object, including constructors, has a "length"
    property whose value is a non-negative integral Number. Unless otherwise
    specified, this value is the number of required parameters shown in the
    subclause heading for the function description. Optional parameters and rest
    parameters are not included in the parameter count.

    Unless otherwise specified, the "length" property of a built-in function
    object has the attributes { [[Writable]]: false, [[Enumerable]]: false,
    [[Configurable]]: true }.
includes: [propertyHelper.js]
---*/

verifyProperty(Date.parse, "length", {
  value: 1,
  writable: false,
  enumerable: false,
  configurable: true,
});
