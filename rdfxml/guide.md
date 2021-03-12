---
title: Parsing Acyclic RDF/XML
author: Luther Tychonievich
...

XMP metadata is the most universally available form of metadata across different media types, and hence the subject of the FHMWG recommendations. XMP is a form of RDF graph serialized as XML, as defined by <https://www.w3.org/TR/rdf-syntax-grammar/>.

[RDF](http://www.w3.org/TR/rdf11-concepts/) is designed to represent generic property graphs, and that flexibility may prove useful in later versions of the FHMWG recommendations because it will allow embedding complicated human relationships and other non-trivial data. However, the subset of XMP recommended by the IPTC, FHMWG, and many other groups is acyclic by construction, allowing a simpler approach to handling the RDF/XML serialization, which this document describes.

Please note: this document's purpose is to define a *minimal* implementation of RDF/XML for the purpose of parsing FHMWG-recommended metadata. It is *recommended* that implementations use a full RDF library for maximum forward-compatibility.

This document assumes understanding of XML, including namespaces.

# Compressed Zebras

RDF is fundamentally organized as a set of assertions.
Each assertion states "the *predicate* of *subject* is *object*",
where subjects and objects may be quite complicated structures
represented as abstract resources described by additional predicates.
For example, to represent a piece of human-language text we'd introduce an abstract resource for the text and then use predicates like "in English is written as", "in Mandarin is written as", and so on to get to the actual text.

The simplest form of RDF/XML is the "striped" or "zebra" form:
a subject contains the predicates it is the subject of, and a predicate contains the objects it applies to which are themselves subjects of other predicates, and so on. The nouns (subjects and objects) in this are either `<rdf:Description>` elements or strings; so for example, "my father's mother's name is Mary" would look like

```xml
<rdf:Description>
 <ex:Father>
  <rdf:Description>
   <ex:Mother>
    <rdf:Description>
     <ex:Name>Mary</ex:Name>
    </rdf:Description>
   </ex:Mother>
  </rdf:Description>
 </ex:Father>
</rdf:Description>
```

When the object of a predicate is a string and the predicate has no attributes, the predicate may be compressed as an attribute of the subject

```xml
<rdf:Description>
 <ex:Father>
  <rdf:Description>
   <ex:Mother>
    <rdf:Description ex:Name="Mary" />
   </ex:Mother>
  </rdf:Description>
 </ex:Father>
</rdf:Description>
```

When the object of a predicate is a language-tagged string, the language is given with an `xml:lang` attribute and a [BCP 47](https://tools.ietf.org/rfc/bcp/bcp47.txt) language tag, and the predicate cannot be compressed as an attribute itself.

```xml
<rdf:Description>
 <ex:Father>
  <rdf:Description>
   <ex:Mother>
    <rdf:Description>
     <ex:Name xml:lang="es">Mary</ex:Name>
    </rdf:Description>
   </ex:Mother>
  </rdf:Description>
 </ex:Father>
</rdf:Description>
```

Explicit RDF datatypes are also handled by an attribute of the predicate (`rdf:datatype`); but explicit RDF datatypes are not used in the current FHMWG recommendations.

When an object is a resource with no attributes, it may be removed by adding `rdf:parseType="Resource"` to the predicate

```xml
<rdf:Description>
 <ex:Father rdf:parseType="Resource">
  <ex:Mother rdf:parseType="Resource">
   <ex:Name xml:lang="es">Mary</ex:Name>
  </ex:Mother>
 </ex:Father>
</rdf:Description>
```

# Hidden types

From the above rules, we can always tell if a given element is a predicate or a resource.

- `rdf:Description` is a resource
- the elements inside a resource are predicates
- predicates have resources inside them *unless* the predicate has `rdf:parseType="Resource"`

So far, resources are of two types:

- `rdf:Decription` elements
- string literals

There is a third type. If a resource is expected and *any* non-`rdf:Description` element is encountered, then the element is equivalent to an `rdf:Description` with an element with a predicate `rdf:type` with `rdf:resource` being the URI created by concatenating the elements' namespace with its name.
This is used extensively in XMP for AltLangs, Bags, and Sequences.
For example,

```xml
<rdf:Description
  xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'
  xmlns:dc='http://purl.org/dc/elements/1.1/'>
 <dc:title>
  <rdf:Alt>
   <rdf:li xml:lang='x-default'>Judy's Rabbit</rdf:li>
  </rdf:Alt>
 </dc:title>
</rdf:Description>
```

is shorthand for

```xml
<rdf:Description
  xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'
  xmlns:dc='http://purl.org/dc/elements/1.1/'>
 <dc:title>
  <rdf:Description>
   <rdf:type rdf:resource="http://www.w3.org/1999/02/22-rdf-syntax-ns#Alt" />
   <rdf:li xml:lang='x-default'>Judy's Rabbit</rdf:li>
  </rdf:Description>
 </dc:title>
</rdf:Description>
```

Note that this rule and the other shortening rules can be combined to get 

```xml
<rdf:Description
  xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'
  xmlns:dc='http://purl.org/dc/elements/1.1/'>
 <dc:title rdf:parseType="Resource">
  <rdf:type rdf:resource="http://www.w3.org/1999/02/22-rdf-syntax-ns#Alt" />
  <rdf:li xml:lang='x-default'>Judy's Rabbit</rdf:li>
 </dc:title>
</rdf:Description>
```


# Pointers = Nesting

Any resource represented as an `rdf:Description` may have an identifier:
either an IRI specified by the attribute `rdf:about` or a file-local identifier specified by the attribute `rdf:nodeID`.

A predicate may be a pointer to a resource instead of having the resources nested inside it by having the attribute `rdf:resource` to point to an IRI-identified resource or `rdf:nodeID` to point to a file-local identifier.

There is no semantic difference between pointing to and containing a resource.

Thus, the following two are semantically equivalent:

```xml
<rdf:Description>
 <ex:Father>
  <rdf:Description>
   <ex:Mother rdf:about="http://example.com/people/my-grandmother">
    <rdf:Description>
     <ex:Name xml:lang="es">Mary</ex:Name>
    </rdf:Description>
   </ex:Mother>
  </rdf:Description>
 </ex:Father>
</rdf:Description>
```

```xml
<rdf:Description>
 <ex:Father rdf:nodeID="12" />
</rdf:Description>
<rdf:Description rdf:nodeID="12">
 <ex:Mother rdf:resource="http://example.com/people/my-grandmother" />
</rdf:Description>
<rdf:Description rdf:about="http://example.com/people/my-grandmother">
 <ex:Name xml:lang="es">Mary</ex:Name>
</rdf:Description>
```

Note that pointers don't need to be to top-level resources; if my grandmother were my landlord I could write

```xml
<rdf:Description>
 <ex:Father>
  <rdf:Description>
   <ex:Mother rdf:about="http://example.com/people/my-grandmother">
    <rdf:Description>
     <ex:Name xml:lang="es">Mary</ex:Name>
    </rdf:Description>
   </ex:Mother>
  </rdf:Description>
 </ex:Father>
 <ex:landlord rdf:resource="http://example.com/people/my-grandmother" />
</rdf:Description>
```

Additionally, IRIs don't have to point to something inside the document at all; they can be pointers to external resources.

# Other shorthands and oddities

## `rdf:ID`

If the document has an `xml:base` attribute,
then `rdf:ID` is shorthand for `rdf:about` where the IRI is the `xml:base` value, a `#`, and then the `rdf:ID` value.

`rdf:ID` can also be given on predicates to reify them, but reification is not a topic currently used by any known XMP vocabulary.


## `rdf:li`

RDF does not recognize order of elements the way XML does.
When an `rdf:li` predicate appears in a document, it is formally shorthand for `rdf:_1`, `rdf:_2`, etc, where the number is the ordinal of this `rdf:li` inside its parent element.

Thus, technically the following are all identical in meaning, though the first should never be written. The third (using only `li`) is recommended.

```
<rdf:Description
  xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'
  xmlns:dc='http://purl.org/dc/elements/1.1/'>
 <dc:title>
  <rdf:Alt>
   <rdf:_3 xml:lang='fr'>Lapin de Judy</rdf:li>
   <rdf:li xml:lang='x-default'>Judy's Rabbit</rdf:li>
   <rdf:li xml:lang='en'>Judy's Rabbit</rdf:li>
  </rdf:Alt>
 </dc:title>
</rdf:Description>
```

```
<rdf:Description
  xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'
  xmlns:dc='http://purl.org/dc/elements/1.1/'>
 <dc:title>
  <rdf:Alt>
   <rdf:_1 xml:lang='x-default'>Judy's Rabbit</rdf:li>
   <rdf:_3 xml:lang='fr'>Lapin de Judy</rdf:li>
   <rdf:_2 xml:lang='en'>Judy's Rabbit</rdf:li>
  </rdf:Alt>
 </dc:title>
</rdf:Description>
```

```
<rdf:Description
  xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'
  xmlns:dc='http://purl.org/dc/elements/1.1/'>
 <dc:title>
  <rdf:Alt>
   <rdf:li xml:lang='x-default'>Judy's Rabbit</rdf:li>
   <rdf:li xml:lang='en'>Judy's Rabbit</rdf:li>
   <rdf:li xml:lang='fr'>Lapin de Judy</rdf:li>
  </rdf:Alt>
 </dc:title>
</rdf:Description>
```

