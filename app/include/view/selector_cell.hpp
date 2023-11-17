/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class SelectorCell : public brls::SelectorCell {
public:
    SelectorCell();

    static View* create();

private:
    brls::Event<int> dismissEvent;
};