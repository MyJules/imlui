-- Fixture for the `manifest` fuzz target. Repeated text, so a .pak seed built from it compresses:
-- tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile
-- tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile tile
return function(props) return ui.Text { text = props.text } end
