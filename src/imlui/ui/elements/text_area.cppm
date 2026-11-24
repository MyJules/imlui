// TextArea: the multi-line TextField, as a typed subclass (TypeBuilder::extends).
module;

export module imlui.ui.elements.text_area;

import imlui.types.type_builder;
import imlui.ui.elements.text_field;

export namespace imlui {

/// TextArea { ... }: every TextField property and signal, multi-line.
class TextArea final : public TextField {
public:
    static void declare(TypeBuilder<TextArea>& type);

    TextArea();
};

} // namespace imlui

module :private;

namespace imlui {

void TextArea::declare(TypeBuilder<TextArea>& type) { type.extends<TextField>(); }

TextArea::TextArea() { m_multiline = true; }

} // namespace imlui
