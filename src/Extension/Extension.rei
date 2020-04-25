module LocalizationDictionary: {
  type t;

  let initial: t;

  let of_yojson: Yojson.Safe.t => t;

  let get: (string, t) => option(string);

  let count: t => int;
};

module LocalizedToken: {
  [@deriving show]
  type t;

  let parse: string => t;

  /**
 [localize(token)] returns a new token that has been localized.
*/
  let localize: (LocalizationDictionary.t, t) => t;

  let decode: Types.Json.decoder(t);

  /*
    [to_string(token)] returns a string representation.

    If the token has been localized with [localize], and has a localization,
    the localized token will be returned.

    Otherwise, the raw token will be returned.
   */
  let to_string: t => string;
};

module Scanner: {
  type category =
    | Default
    | Bundled
    | User
    | Development;

  type t =
    pri {
      category,
      manifest: Manifest.t,
      path: string,
    };

  let load:
    (~prefix: option(string)=?, ~category: category, string) => option(t);
  let scan:
    (~prefix: option(string)=?, ~category: category, string) => list(t);
};
