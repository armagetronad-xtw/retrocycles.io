// The game's own coloured text (0xRRGGBB / 0xRESETT codes), rendered through
// AA.colorSpans from aa_api.js. Before the wrapper is loaded the codes are stripped
// with the same expression the wrapper uses, so nothing ever shows a raw code.
const CODE = /0x(?:[0-9a-fA-F]{6}|RESETT)/g;

export function stripColors(text: string): string {
  const AA = window.AA;
  return AA ? AA.stripColors(text) : String(text || "").replace(CODE, "");
}

export function Colored({ text }: { text: string }) {
  const AA = window.AA;
  if (!AA) return <>{stripColors(text)}</>;
  return (
    <>
      {AA.colorSpans(text).map(([part, color], i) =>
        color ? (
          <span key={i} style={{ color }}>
            {part}
          </span>
        ) : (
          <span key={i}>{part}</span>
        )
      )}
    </>
  );
}
