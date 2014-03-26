<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:fo="http://www.w3.org/1999/XSL/Format"
                version="1.0">
 <xsl:param name="use.id.as.filename" select="'1'"/>
 <xsl:param name="admon.graphics" select="'1'"/>
 <xsl:param name="admon.graphics.path"></xsl:param>
 <xsl:param name="chunk.section.depth" select="2"></xsl:param>
 <xsl:param name="chunk.quietly">1</xsl:param>
 <xsl:param name="html.stylesheet"
    select="'style.css'"/>
 <xsl:param name="section.autolabel" select="1"/>
 <xsl:param name="table.section.depth" select="1"/>
 <xsl:param name="toc.section.depth" select="5"/>
 <xsl:template name="user.header.content">
  <link href="../style.css" title="walsh" rel="stylesheet" type="text/css"/>
 </xsl:template>
</xsl:stylesheet>